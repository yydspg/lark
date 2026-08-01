// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Column Engine v3: Two-layer architecture
//
// Layer 1 (node/)  — Business nodes: define WHAT to compute
// Layer 2 (compute/) — Compute kernels: HOW to compute (SIMD-optimized)
//
// Data flows as TensorTable between nodes.
// Operator overloads are provided for ergonomic tensor expressions.

// Data container
#include "column/tensor.h"
#include "column/tensor_table.h"

// Compute kernels
#include "column/compute/simd.h"
#include "column/compute/add_tensor.h"
#include "column/compute/sub_tensor.h"
#include "column/compute/mul_tensor.h"
#include "column/compute/div_tensor.h"
#include "column/compute/scalar_tensor.h"
#include "column/compute/reduce_tensor.h"
#include "column/compute/compare_tensor.h"
#include "column/compute/unary_tensor.h"

// Business nodes
#include "column/node/node.h"
#include "column/node/transform_node.h"
#include "column/node/filter_node.h"
#include "column/node/aggregate_node.h"

// Graph (pipeline of nodes)
#include "column/graph.h"

// ─────────────────────────────────────────────────────────────────────────────
// Convenience operator overloads
// These delegate to the compute layer so users can write natural expressions.
// ─────────────────────────────────────────────────────────────────────────────
namespace lark::column {

inline Tensor operator+(const Tensor& a, const Tensor& b) { return compute::add(a, b); }
inline Tensor operator-(const Tensor& a, const Tensor& b) { return compute::sub(a, b); }
inline Tensor operator*(const Tensor& a, const Tensor& b) { return compute::mul(a, b); }
inline Tensor operator/(const Tensor& a, const Tensor& b) { return compute::div(a, b); }

inline Tensor operator+(const Tensor& a, double v) { return compute::add_scalar(a, v); }
inline Tensor operator-(const Tensor& a, double v) { return compute::sub_scalar(a, v); }
inline Tensor operator*(const Tensor& a, double v) { return compute::mul_scalar(a, v); }
inline Tensor operator/(const Tensor& a, double v) { return compute::div_scalar(a, v); }
inline Tensor operator+(double v, const Tensor& a) { return compute::add_scalar(a, v); }
inline Tensor operator*(double v, const Tensor& a) { return compute::mul_scalar(a, v); }
inline Tensor operator-(const Tensor& a) { return compute::neg(a); }

// Version info
constexpr int kVersionMajor = 3;
constexpr int kVersionMinor = 0;
constexpr int kVersionPatch = 0;

}  // namespace lark::column
