// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/graph.h"

namespace lark::column {

Graph& Graph::add_node(std::unique_ptr<Node> node) {
  nodes_.push_back(std::move(node));
  return *this;
}

TensorTable Graph::execute(TensorTable input) const {
  for (const auto& node : nodes_) {
    input = node->execute(std::move(input));
  }
  return input;
}

}  // namespace lark::column
