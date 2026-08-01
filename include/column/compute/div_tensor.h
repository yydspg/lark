// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once
#include "column/tensor.h"

namespace lark::column::compute {
// Element-wise division: result[i] = a[i] / b[i]
// int64 / int64 → double; double / double → double
Tensor div(const Tensor& a, const Tensor& b);
}  // namespace lark::column::compute
