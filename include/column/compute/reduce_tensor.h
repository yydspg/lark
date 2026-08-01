// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once
#include "column/tensor.h"

namespace lark::column::compute {
// Reduction operations
double sum(const Tensor& a);
double mean(const Tensor& a);
int64_t count(const Tensor& a);
}  // namespace lark::column::compute
