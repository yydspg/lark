// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once
#include "column/tensor.h"

namespace lark::column::compute {
// Unary operations
Tensor neg(const Tensor& a);
Tensor abs(const Tensor& a);
Tensor sqrt(const Tensor& a);   // always returns double
Tensor square(const Tensor& a);
}  // namespace lark::column::compute
