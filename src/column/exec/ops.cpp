// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Execution-layer op factory (CPU backend).
//
// create_op() dispatches to the per-family factories so each op family stays in
// its own translation unit (see ops_kernels.h and ops_{binary,unary,reduce,
// compare,special}.cpp) — keeping the execution layer modular.

#include <stdexcept>

#include "column/exec/ops_kernels.h"
#include "column/exec/tensor_op.h"

namespace lark::column::exec {

std::unique_ptr<TensorOp> create_op(const OpSpec& spec) {
  if (auto op = create_binary_op(spec)) return op;
  if (auto op = create_unary_op(spec)) return op;
  if (auto op = create_reduce_op(spec)) return op;
  if (auto op = create_compare_op(spec)) return op;
  if (auto op = create_special_op(spec)) return op;
  throw std::invalid_argument("create_op: unknown op type '" + spec.type + "'");
}

}  // namespace lark::column::exec
