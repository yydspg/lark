// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include "column/node/node.h"

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// FilterNode: business node that filters rows by a mask expression.
//
// The user-supplied function returns an int64 mask tensor (non-zero = keep).
// Internally the node copies only the passing rows across ALL columns.
// ─────────────────────────────────────────────────────────────────────────────
class FilterNode : public Node {
 public:
  using MaskFn = std::function<Tensor(const TensorTable&)>;

  explicit FilterNode(MaskFn mask_fn);

  TensorTable execute(TensorTable input) const override;
  const char* node_type() const noexcept override;

 private:
  MaskFn mask_fn_;
};

}  // namespace lark::column
