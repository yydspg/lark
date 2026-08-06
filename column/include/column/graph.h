// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <vector>

#include "column/node/node.h"

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// Graph: pipeline of business Nodes.
//
// Nodes are executed sequentially.  Each node consumes the output of the
// previous one and produces a new TensorTable.
//
// Usage:
//   Graph g;
//   g.add_node(std::make_unique<TransformNode>("y",
//         [](const TensorTable& t) { return compute::mul_scalar(t.get("x"), 2); }));
//   g.add_node(std::make_unique<FilterNode>(
//         [](const TensorTable& t) { return compute::gt_scalar(t.get("y"), 10); }));
//   auto result = g.execute(std::move(input));
// ─────────────────────────────────────────────────────────────────────────────
class Graph {
 public:
  // Add a business node to the pipeline
  Graph& add_node(std::unique_ptr<Node> node);

  // Execute the full pipeline
  TensorTable execute(TensorTable input) const;

  // Info
  size_t num_nodes() const { return nodes_.size(); }
  void clear() { nodes_.clear(); }

 private:
  std::vector<std::unique_ptr<Node>> nodes_;
};

}  // namespace lark::column
