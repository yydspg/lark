// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

#include "column/exec/tensor_op.h"

namespace lark::column::biz::dsl {

// ─────────────────────────────────────────────────────────────────────────────
// A tiny expression DSL for declaring module sub-graphs.
//
// Each statement is an assignment; identifiers are column (tensor) names,
// numbers are scalar constants, and function calls map to built-in ops:
//
//   // inputs: {x, y}   outputs: {score}
//   s1     = x * 2
//   s2     = s1 + y
//   mask   = x > 10
//   score  = select(mask, s2, 0)
//   total  = sum(score)
//
// Supported functions: sum, mean, count, max, min, stddev, abs, neg, square,
// sqrt, exp, log, select, filter, cast, quantize, dequantize, dot.
//
// parse() returns the ordered business ops; throws std::invalid_argument on a
// syntax error.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<exec::OpSpec> parse(const std::string& source);

}  // namespace lark::column::biz::dsl
