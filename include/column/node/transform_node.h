// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <string>

#include "column/node/node.h"

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// TransformNode: business node that adds / replaces a column.
//
// The user-supplied function receives the current table and returns a new
// Tensor.  The compute layer (compute::add, compute::mul, …) is used inside
// the lambda to perform the actual vector math.
// ─────────────────────────────────────────────────────────────────────────────
class TransformNode : public Node {
 public:
  using TransformFn = std::function<Tensor(const TensorTable&)>;

  TransformNode(std::string output_name, TransformFn fn);

  TensorTable execute(TensorTable input) const override;
  const char* node_type() const noexcept override;

 private:
  std::string output_name_;
  TransformFn fn_;
};

}  // namespace lark::column
