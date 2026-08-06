// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once
#include "column/tensor.h"

namespace lark::column::compute {
// Element-wise comparison → int64 mask (1 = true, 0 = false)
Tensor gt(const Tensor& a, const Tensor& b);
Tensor lt(const Tensor& a, const Tensor& b);
Tensor ge(const Tensor& a, const Tensor& b);
Tensor le(const Tensor& a, const Tensor& b);
Tensor eq(const Tensor& a, const Tensor& b);

// Scalar comparison → int64 mask
Tensor gt_scalar(const Tensor& a, double v);
Tensor lt_scalar(const Tensor& a, double v);
Tensor ge_scalar(const Tensor& a, double v);
Tensor le_scalar(const Tensor& a, double v);
Tensor eq_scalar(const Tensor& a, double v);
}  // namespace lark::column::compute
