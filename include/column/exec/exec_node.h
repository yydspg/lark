// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "column/exec/tensor_op.h"
#include "dag/coro/async_event.h"

namespace lark::column::exec {

// Execution status of a compute node within one graph run.
enum class NodeStatus { kPending, kSuccess, kFailed };

// ─────────────────────────────────────────────────────────────────────────────
// ComputeNode: the execution-layer node. Binds exactly one TensorOp.
//
// The node knows nothing about business semantics — it simply carries an op,
// a framework-assigned id, the owning module name (used for monitoring) and
// its dependency edges. The framework (ComputeGraph / Pipeline) wires nodes
// together; business code never constructs these directly.
// ─────────────────────────────────────────────────────────────────────────────
class ComputeNode {
 public:
  ComputeNode(std::string id, std::string module, std::unique_ptr<TensorOp> op);

  const std::string& id() const noexcept { return id_; }
  const std::string& module() const noexcept { return module_; }

  TensorOp& op() noexcept { return *op_; }
  const TensorOp& op() const noexcept { return *op_; }

  // ---- dependency edges (framework-managed) -----------------------------
  void add_dependency(ComputeNode* dep) { deps_.push_back(dep); }
  const std::vector<ComputeNode*>& dependencies() const noexcept {
    return deps_;
  }

  // ---- run state --------------------------------------------------------
  NodeStatus status() const noexcept {
    return status_.load(std::memory_order_acquire);
  }
  std::chrono::nanoseconds elapsed() const noexcept { return elapsed_; }
  const std::exception_ptr& error() const noexcept { return error_; }

  void SetStatus(NodeStatus s);
  void SetElapsed(std::chrono::nanoseconds e);
  void SetError(std::exception_ptr e);

  // Completion signal awaited by downstream nodes (coroutine async path).
  coro::AsyncEvent& done_event() noexcept { return done_; }
  void ResetRunState();

 private:
  std::string id_;
  std::string module_;
  std::unique_ptr<TensorOp> op_;
  std::vector<ComputeNode*> deps_;

  coro::AsyncEvent done_;
  std::atomic<NodeStatus> status_{NodeStatus::kPending};
  std::chrono::nanoseconds elapsed_{0};
  std::exception_ptr error_;
};

}  // namespace lark::column::exec
