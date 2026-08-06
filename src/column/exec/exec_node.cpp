// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/exec/exec_node.h"

#include <utility>

namespace lark::column::exec {

ComputeNode::ComputeNode(std::string id, std::string module,
                         std::unique_ptr<TensorOp> op)
    : id_(std::move(id)),
      module_(std::move(module)),
      op_(std::move(op)) {}

void ComputeNode::SetStatus(NodeStatus s) {
  status_.store(s, std::memory_order_release);
}

void ComputeNode::SetElapsed(std::chrono::nanoseconds e) { elapsed_ = e; }

void ComputeNode::SetError(std::exception_ptr e) { error_ = std::move(e); }

void ComputeNode::ResetRunState() {
  status_.store(NodeStatus::kPending, std::memory_order_release);
  elapsed_ = std::chrono::nanoseconds::zero();
  error_ = nullptr;
  done_.Reset();
}

}  // namespace lark::column::exec
