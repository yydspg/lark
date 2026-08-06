// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "column/backend/backend.h"
#include "column/biz/module.h"
#include "column/exec/compute_graph.h"

namespace lark::column::biz {

// Per-module compile-time info: which node ids produce the module's declared
// outputs and which node is its "tail" (used for depends_on ordering edges).
struct CompiledModule {
  std::string name;
  std::vector<std::string> output_names;
  std::string tail;                        // last op node id
  std::vector<std::string> output_nodes;   // node ids producing declared outputs
};

// ─────────────────────────────────────────────────────────────────────────────
// PipelineCompiler: turns a set of business Modules into an execution
// ComputeGraph (总图编排).
//
// This is the framework side of the wiring contract: modules only declare
// inputs / sub-graph / outputs; the compiler resolves
//   * anonymous input boundary nodes (<module>/@in:<port>),
//   * anonymous output pass-through nodes (<module>/@out:<port>),
//   * anonymous ordering nodes (<module>/@dep:<module>),
//   * module-local temp columns (@<module>/tN for DSL temporaries), and
//   * data-flow dependency edges between modules (by produced column).
//
// Modules are compiled in REGISTRATION order. When a module references a
// column produced by a module compiled later (a forward reference), an
// anonymous **placeholder node** is created to stand in (占位), keeping the
// graph buildable; once the producer module is compiled, the placeholder is
// replaced by the real node (最终替换) and every edge is rewired. depends_on
// edges to later modules use a placeholder tail the same way. Cycles are
// detected by ComputeGraph::Finalize (topological sort).
//
// Extracted from Pipeline so the orchestration class stays small and each
// responsibility (config + feed/compute/fetch vs. graph construction) lives in
// its own class.
// ─────────────────────────────────────────────────────────────────────────────
class PipelineCompiler {
 public:
  PipelineCompiler(exec::ComputeGraph& graph, backend::Backend& backend);

  // Compile the given modules (in registration order) into the graph.
  // Throws std::invalid_argument / std::runtime_error on wiring problems,
  // including unresolved forward references (a referenced column/tail whose
  // producer never appeared).
  void Compile(const std::vector<std::shared_ptr<Module>>& modules,
               const std::unordered_set<std::string>& module_names);

  // Every column produced by any module, mapped to its producing node id.
  const std::unordered_map<std::string, std::string>& column_producers() const {
    return column_producer_;
  }

 private:
  void CollectColumnProducers(const std::vector<std::shared_ptr<Module>>& modules);
  void CompileModule(const std::shared_ptr<Module>& module);
  std::string ResolveInputDep(
      const std::string& col,
      const std::unordered_map<std::string, std::string>& local,
      const std::string& module_name);
  // Ensure a placeholder node exists for a column produced by a module that has
  // not been compiled yet; returns its node id.
  std::string EnsureColumnPlaceholder(const std::string& col);
  // Record that `node_id` produces `col`; if a placeholder was standing in for
  // it, replace the placeholder with the real node.
  void RegisterProducer(const std::string& col, const std::string& node_id,
                        const std::string& module_name);
  static std::string RemapTemp(
      const std::string& col, const std::string& module_name,
      std::unordered_map<std::string, std::string>& rename);

  exec::ComputeGraph& graph_;
  backend::Backend& backend_;

  std::unordered_map<std::string, std::string> column_producer_;  // col -> node id
  std::unordered_map<std::string, std::string> column_module_;    // col -> module
  std::unordered_map<std::string, CompiledModule> module_info_;   // module -> info
  // col -> placeholder node id (created for forward references)
  std::unordered_map<std::string, std::string> column_placeholder_;
  // module -> placeholder tail node id (created for forward depends_on)
  std::unordered_map<std::string, std::string> module_tail_placeholder_;
};

}  // namespace lark::column::biz
