// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once
#include "column/tensor.h"

namespace lark::column::compute {
// Element-wise multiplication: result[i] = a[i] * b[i]
Tensor mul(const Tensor& a, const Tensor& b);
}  // namespace lark::column::compute
