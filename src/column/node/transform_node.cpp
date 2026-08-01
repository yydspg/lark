// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/node/transform_node.h"

namespace lark::column {

TransformNode::TransformNode(std::string output_name, TransformFn fn)
    : output_name_(std::move(output_name)), fn_(std::move(fn)) {}

TensorTable TransformNode::execute(TensorTable input) const {
  Tensor result = fn_(input);
  input.add(output_name_, std::move(result));
  return input;
}

const char* TransformNode::node_type() const noexcept { return "TransformNode"; }

}  // namespace lark::column
