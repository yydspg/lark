// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>

#include "column/tensor_table.h"

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// Node: abstract business node.
//
// Layer 1 (业务层) — defines WHAT to compute.
// Subclasses specify the rule; Layer 2 (compute层) does the actual math.
// ─────────────────────────────────────────────────────────────────────────────
class Node {
 public:
  virtual ~Node() = default;

  // Execute this node's logic: consume input, produce output
  virtual TensorTable execute(TensorTable input) const = 0;

  // Node type name (for debugging / introspection)
  virtual const char* node_type() const noexcept = 0;
};

}  // namespace lark::column
