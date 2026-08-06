// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "column/context/execution_context.h"
#include "column/exec/exec_node.h"
#include "coro/fire_and_forget.h"
#include "coro/thread_pool.h"

namespace lark::column::exec {

// ─────────────────────────────────────────────────────────────────────────────
// ComputeGraph: the execution-layer DAG of ComputeNodes.
//
// Mirrors the design of TensorFlow-style graph execution but wired onto the
// project's coroutine machinery:
//
//   * execute()       — deterministic sequential run in topological order on
//                       the calling thread.
//   * execute_async() — fully asynchronous: one detached coroutine per node;
//                       each coroutine co_awaits its dependencies' completion
//                       events, then runs its op on the compute pool. Nodes
//                       whose dependencies are ready execute concurrently.
//
// The graph is reused across runs: ResetRunState() clears per-node run state
// at the start of every execution.
// ─────────────────────────────────────────────────────────────────────────────
class ComputeGraph {
 public:
  ComputeGraph() = default;
  ComputeGraph(const ComputeGraph&) = delete;
  ComputeGraph& operator=(const ComputeGraph&) = delete;
  ComputeGraph(ComputeGraph&&) noexcept = default;
  ComputeGraph& operator=(ComputeGraph&&) noexcept = default;

  // Add a node. `module` is the owning business module name (for monitoring).
  // The returned node must be wired via AddDependency() before finalize().
  ComputeNode& AddNode(std::string id, std::string module,
                       std::unique_ptr<TensorOp> op);

  ComputeNode* Find(const std::string& id) const;
  const std::vector<std::unique_ptr<ComputeNode>>& nodes() const noexcept {
    return nodes_;
  }
  size_t size() const noexcept { return nodes_.size(); }
  bool empty() const noexcept { return nodes_.empty(); }
  bool finalized() const noexcept { return finalized_; }

  // Wire a dependency edge (both ids must exist). Idempotent.
  void AddDependency(const std::string& id, const std::string& dep_id);

  // Topologically sort and detect cycles. Throws std::runtime_error on a
  // cycle. Must be called before execute(); called automatically if not.
  void Finalize();

  // Sequential execution (topological order, calling thread).
  void Execute(context::ExecutionContext& ctx);

  // Asynchronous coroutine execution on the given compute pool.
  void ExecuteAsync(coro::ThreadPool& pool, context::ExecutionContext& ctx);

 private:
  struct RunState;
  struct ModuleAcc;

  void ResetRunState();

  void ReportRunSummary(context::ExecutionContext& ctx, const RunState& state,
                        std::chrono::nanoseconds wall, std::size_t workers);

  coro::FireAndForget SpawnNode(ComputeNode* node, coro::ThreadPool& pool,
                                context::ExecutionContext& ctx,
                                RunState& state);

  std::vector<std::unique_ptr<ComputeNode>> nodes_;
  std::unordered_map<std::string, ComputeNode*> by_id_;
  std::vector<ComputeNode*> topo_;
  bool finalized_ = false;
};

}  // namespace lark::column::exec
