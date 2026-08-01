// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once
#include "column/tensor.h"

namespace lark::column::compute {
// Scalar broadcasting: result[i] = tensor[i] op scalar
Tensor add_scalar(const Tensor& a, double v);
Tensor sub_scalar(const Tensor& a, double v);
Tensor mul_scalar(const Tensor& a, double v);
Tensor div_scalar(const Tensor& a, double v);
}  // namespace lark::column::compute
