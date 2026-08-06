// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/biz/pipeline_compiler.h"

#include <queue>
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

  CollectColumnProducers(modules);
  const auto order = TopoSortModules(modules, module_names);
  for (const auto& module : order) {
    CompileModule(module);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass A: detect ambiguous column producers.
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
// Pass B: topological order of modules by explicit depends_on + data flow.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::shared_ptr<Module>> PipelineCompiler::TopoSortModules(
    const std::vector<std::shared_ptr<Module>>& modules,
    const std::unordered_set<std::string>& module_names) const {
  std::unordered_map<std::string, std::unordered_set<std::string>> deps;
  for (const auto& module : modules) {
    deps[module->name()];
    for (const auto& dep : module->dependencies())
      deps[module->name()].insert(dep);

    auto consumes = [&](const std::string& col) {
      auto it = column_module_.find(col);
      if (it == column_module_.end()) return;  // feed port
      const std::string& prod = it->second;
      if (prod != module->name()) deps[module->name()].insert(prod);
    };
    for (const auto& port : module->inputs()) consumes(port);
    for (const auto& spec : module->subgraph().ops()) {
      for (const auto& in : spec.inputs) {
        if (in.empty() || in.front() != '@') consumes(in);
      }
    }
  }

  for (const auto& module : modules) {
    for (const auto& dep : module->dependencies()) {
      if (module_names.count(dep) == 0)
        throw std::invalid_argument("PipelineCompiler: module '" + module->name() +
                                    "' depends_on unknown module '" + dep + "'");
    }
  }

  std::unordered_map<std::string, std::size_t> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> outs;
  for (const auto& module : modules) {
    for (const auto& dep : deps[module->name()]) {
      outs[dep].push_back(module->name());
      in_degree[module->name()]++;
    }
  }

  std::queue<std::string> ready;
  std::vector<std::shared_ptr<Module>> order;
  std::unordered_set<std::string> visited;
  for (const auto& module : modules) {
    if (in_degree[module->name()] == 0) ready.push(module->name());
  }
  while (!ready.empty()) {
    const std::string name = ready.front();
    ready.pop();
    for (const auto& module : modules) {
      if (module->name() == name && visited.insert(name).second) {
        order.push_back(module);
        break;
      }
    }
    for (const auto& next : outs[name]) {
      if (--in_degree[next] == 0) ready.push(next);
    }
  }

  if (order.size() != modules.size()) {
    throw std::runtime_error("PipelineCompiler: module dependency cycle detected");
  }
  return order;
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

std::string PipelineCompiler::ResolveInputDep(
    const std::string& col,
    const std::unordered_map<std::string, std::string>& local,
    const std::string& module_name) const {
  auto lit = local.find(col);
  if (lit != local.end()) return lit->second;
  auto pit = column_producer_.find(col);
  if (pit != column_producer_.end()) return pit->second;
  // Remaining columns are feed ports: data is seeded before compute() runs,
  // so no dependency edge is required.
  (void)module_name;
  return "";
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass C: materialize a module's nodes into the graph.
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
    auto pit = column_producer_.find(port);
    if (pit != column_producer_.end() &&
        column_module_.at(port) != module->name()) {
      graph_.AddDependency(node_id, pit->second);
    }
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
      column_producer_[out] = node_id;
      column_module_[out] = module->name();
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
      auto pit = column_producer_.find(port);
      if (pit != column_producer_.end() &&
          column_module_.at(port) != module->name()) {
        graph_.AddDependency(node_id, pit->second);
      }
      local[port] = node_id;
      column_producer_[port] = node_id;
      column_module_[port] = module->name();
      cm.output_nodes.push_back(node_id);
      cm.tail = node_id;
    }
    cm.output_names.push_back(port);
  }

  // ---- explicit module ordering edges (anonymous) -----------------------
  for (const auto& dep_name : module->dependencies()) {
    const std::string node_id = module->name() + "/@dep:" + dep_name;
    add_node(node_id,
             exec::OpSpec{"identity", {module->name()}, {module->name()}});
    const auto& dep_info = module_info_.at(dep_name);
    if (!dep_info.output_nodes.empty()) {
      for (const auto& out_node : dep_info.output_nodes)
        graph_.AddDependency(node_id, out_node);
    } else if (!dep_info.tail.empty()) {
      graph_.AddDependency(node_id, dep_info.tail);
    }
    if (!first_node.empty() && first_node != node_id)
      graph_.AddDependency(first_node, node_id);
    if (cm.tail.empty()) cm.tail = node_id;
  }

  module_info_[module->name()] = std::move(cm);
}

}  // namespace lark::column::biz
