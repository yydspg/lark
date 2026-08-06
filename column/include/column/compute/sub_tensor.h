// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once
#include "column/tensor.h"

namespace lark::column::compute {
// Element-wise subtraction: result[i] = a[i] - b[i]
Tensor sub(const Tensor& a, const Tensor& b);
}  // namespace lark::column::compute
