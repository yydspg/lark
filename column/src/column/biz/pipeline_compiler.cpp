// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/biz/pipeline_compiler.h"

#include <stdexcept>

namespace lark::column::biz {

PipelineCompiler::PipelineCompiler(exec::ComputeGraph& graph,
                                   backend::Backend& backend)
    : graph_(graph), backend_(backend) {}

void PipelineCompiler::Compile(
    const std::vector<std::shared_ptr<Module>>& modules,
    const std::unordered_set<std::string>& module_names) {
  column_producer_.clear();
  column_module_.clear();
  module_info_.clear();
  column_placeholder_.clear();
  module_tail_placeholder_.clear();

  // Pass 1: every column any module produces -> producing module.
  CollectColumnProducers(modules);

  // Validate explicit depends_on targets exist.
  for (const auto& module : modules) {
    for (const auto& dep : module->dependencies()) {
      if (module_names.count(dep) == 0) {
        throw std::invalid_argument("PipelineCompiler: module '" +
                                    module->name() + "' depends_on unknown module '" +
                                    dep + "'");
      }
    }
  }

  // Pass 2: compile in REGISTRATION order. Forward references (a column or a
  // depends_on tail of a not-yet-compiled module) get anonymous placeholder
  // nodes; they are replaced once the real producer compiles.
  for (const auto& module : modules) {
    CompileModule(module);
  }

  if (graph_.HasPlaceholders()) {
    throw std::runtime_error(
        "PipelineCompiler: unresolved forward references (a referenced "
        "column/tail was never produced)");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 1: detect ambiguous column producers.
// ─────────────────────────────────────────────────────────────────────────────
void PipelineCompiler::CollectColumnProducers(
    const std::vector<std::shared_ptr<Module>>& modules) {
  for (const auto& module : modules) {
    std::unordered_set<std::string> written;
    for (const auto& port : module->outputs()) written.insert(port);
    for (const auto& spec : module->subgraph().ops()) {
      for (const auto& out : spec.outputs) written.insert(out);
    }
    for (const auto& col : written) {
      if (col.empty() || col.front() == '@') continue;  // module-local temp
      auto [it, inserted] = column_module_.emplace(col, module->name());
      if (!inserted && it->second != module->name()) {
        throw std::invalid_argument(
            "PipelineCompiler: column '" + col + "' produced by both '" +
            it->second + "' and '" + module->name() + "'");
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward-reference helpers (placeholder nodes, 占位 -> 替换)
// ─────────────────────────────────────────────────────────────────────────────

std::string PipelineCompiler::EnsureColumnPlaceholder(const std::string& col) {
  auto it = column_placeholder_.find(col);
  if (it != column_placeholder_.end()) return it->second;
  const std::string id = "@placeholder:" + col;
  graph_.AddPlaceholder(col, id);
  column_placeholder_[col] = id;
  return id;
}

void PipelineCompiler::RegisterProducer(const std::string& col,
                                        const std::string& node_id,
                                        const std::string& module_name) {
  column_producer_[col] = node_id;
  column_module_[col] = module_name;
  auto it = column_placeholder_.find(col);
  if (it != column_placeholder_.end()) {
    graph_.ReplacePlaceholder(it->second, node_id);  // 最终替换
    column_placeholder_.erase(it);
  }
}

std::string PipelineCompiler::ResolveInputDep(
    const std::string& col,
    const std::unordered_map<std::string, std::string>& local,
    const std::string& module_name) {
  auto lit = local.find(col);
  if (lit != local.end()) return lit->second;
  auto pit = column_producer_.find(col);
  if (pit != column_producer_.end()) return pit->second;
  auto mit = column_module_.find(col);
  if (mit != column_module_.end() && mit->second != module_name) {
    // Produced by another module: if already compiled this returns its node via
    // column_producer_ above; otherwise a placeholder stands in until then.
    return EnsureColumnPlaceholder(col);
  }
  // Remaining columns are feed ports: data is seeded before compute() runs, so
  // no dependency edge is required.
  return "";
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string PipelineCompiler::RemapTemp(
    const std::string& col, const std::string& module_name,
    std::unordered_map<std::string, std::string>& rename) {
  auto it = rename.find(col);
  if (it != rename.end()) return it->second;
  const std::string mapped = "@" + module_name + "/" + col.substr(1);
  rename[col] = mapped;
  return mapped;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 2: materialize a module's nodes into the graph.
// ─────────────────────────────────────────────────────────────────────────────
void PipelineCompiler::CompileModule(const std::shared_ptr<Module>& module) {
  CompiledModule cm;
  cm.name = module->name();

  std::unordered_map<std::string, std::string> local;   // col -> node id
  std::unordered_map<std::string, std::string> rename;  // DSL temp rename
  std::string first_node;

  auto add_node = [&](const std::string& node_id, exec::OpSpec spec) {
    graph_.AddNode(node_id, module->name(),
                   backend_.CreateOp(std::move(spec)));
    if (first_node.empty()) first_node = node_id;
  };
  auto remap = [&](std::string col) {
    if (!col.empty() && col.front() == '@')
      return RemapTemp(col, module->name(), rename);
    return col;
  };

  // ---- anonymous input boundary nodes (framework-managed wiring) --------
  for (const auto& port : module->inputs()) {
    const std::string node_id = module->name() + "/@in:" + port;
    add_node(node_id, exec::OpSpec{"identity", {port}, {port}});
    const std::string dep = ResolveInputDep(port, local, module->name());
    if (!dep.empty()) graph_.AddDependency(node_id, dep);
    local[port] = node_id;
  }

  // ---- sub-graph ops ----------------------------------------------------
  size_t idx = 0;
  for (const auto& spec : module->subgraph().ops()) {
    exec::OpSpec ns = spec;
    for (auto& in : ns.inputs) in = remap(std::move(in));
    for (auto& out : ns.outputs) out = remap(std::move(out));

    const std::string node_id = module->name() + "/op" + std::to_string(idx++);

    // Materialize the execution node first (so dependency edges can resolve),
    // keeping `ns` intact for wiring.
    graph_.AddNode(node_id, module->name(), backend_.CreateOp(ns));

    for (const auto& in : ns.inputs) {
      const std::string dep = ResolveInputDep(in, local, module->name());
      if (!dep.empty()) graph_.AddDependency(node_id, dep);
    }
    for (const auto& out : ns.outputs) {
      if (out.empty()) continue;
      if (local.count(out) > 0) {
        throw std::invalid_argument(
            "PipelineCompiler: column '" + out + "' written twice in module '" +
            module->name() + "'");
      }
      local[out] = node_id;
      RegisterProducer(out, node_id, module->name());
    }
    cm.tail = node_id;
  }

  // ---- declared outputs --------------------------------------------------
  for (const auto& port : module->outputs()) {
    auto it = local.find(port);
    if (it != local.end()) {
      cm.output_nodes.push_back(it->second);
    } else {
      // Pass-through output (an input port or a feed column): publish it via
      // an anonymous output node.
      const std::string node_id = module->name() + "/@out:" + port;
      add_node(node_id, exec::OpSpec{"identity", {port}, {port}});
      const std::string dep = ResolveInputDep(port, local, module->name());
      if (!dep.empty()) graph_.AddDependency(node_id, dep);
      local[port] = node_id;
      RegisterProducer(port, node_id, module->name());
      cm.output_nodes.push_back(node_id);
      cm.tail = node_id;
    }
    cm.output_names.push_back(port);
  }

  // Ensure a tail exists (a module with no ops/outputs still needs a node so
  // forward depends_on placeholders can be replaced by a real node).
  if (cm.tail.empty()) {
    const std::string node_id = module->name() + "/@noop";
    add_node(node_id, exec::OpSpec{"identity", {"@noop:" + module->name()},
                                   {"@noop:" + module->name()}});
    cm.tail = node_id;
  }

  // ---- explicit module ordering edges (anonymous) -----------------------
  for (const auto& dep_name : module->dependencies()) {
    const std::string node_id = module->name() + "/@dep:" + dep_name;
    add_node(node_id,
             exec::OpSpec{"identity", {module->name()}, {module->name()}});
    auto info_it = module_info_.find(dep_name);
    if (info_it != module_info_.end()) {
      // dependency module already compiled: order after its outputs/tail
      const auto& dep_info = info_it->second;
      if (!dep_info.output_nodes.empty()) {
        for (const auto& out_node : dep_info.output_nodes)
          graph_.AddDependency(node_id, out_node);
      } else if (!dep_info.tail.empty()) {
        graph_.AddDependency(node_id, dep_info.tail);
      }
    } else {
      // dependency module comes later: a placeholder tail stands in, replaced
      // when that module compiles.
      std::string& tail_ph = module_tail_placeholder_[dep_name];
      if (tail_ph.empty()) {
        tail_ph = "@tail:" + dep_name;
        graph_.AddPlaceholder("@tail:" + dep_name, tail_ph);
      }
      graph_.AddDependency(node_id, tail_ph);
    }
    if (!first_node.empty() && first_node != node_id)
      graph_.AddDependency(first_node, node_id);
    if (cm.tail.empty()) cm.tail = node_id;
  }

  // Replace any forward depends_on placeholder tail for this module.
  auto tail_it = module_tail_placeholder_.find(module->name());
  if (tail_it != module_tail_placeholder_.end() && !cm.tail.empty()) {
    graph_.ReplacePlaceholder(tail_it->second, cm.tail);
    module_tail_placeholder_.erase(tail_it);
  }

  module_info_[module->name()] = std::move(cm);
}

}  // namespace lark::column::biz
