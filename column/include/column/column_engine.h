// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Column Engine v4: feed → compute → fetch, with business/execution layers.
//
//   feed    (feed_fetch.h)     行转列 — row-oriented business records become
//                               columnar tensors in the execution store.
//   compute (pipeline.h)       图节点编排计算 — modules declare sub-graphs; the
//                               framework wires them into a global ComputeGraph
//                               (with anonymous/temp nodes) and runs it on the
//                               coroutine compute pool.
//   fetch   (feed_fetch.h)     列转行 — result columns are materialized back to
//                               business rows.
//
// Layers:
//   Business (biz/)   — Module / SubGraph / DSL / Pipeline: WHAT to compute,
//                       ergonomic, framework handles wiring & readiness.
//   Execution (exec/) — TensorOp / ComputeNode / ComputeGraph: HOW to compute,
//                       performance-focused kernels, bind tensor ops to nodes.
//   Backend (backend/) — factory for compute backends (CPU implemented).
//   Context (context/) — per-run ExecutionContext with feed/compute/fetch phases.
//   Monitoring (monitor.h) — unified lark::metric::Monitor + column StatsCollector.

// Data containers
#include "column/tensor.h"
#include "column/tensor_table.h"
#include "column/tensor_store.h"
#include "column/types.h"

// Row <-> column conversion (feed / fetch)
#include "column/feed_fetch.h"

// Compute kernels (Layer 2)
#include "column/compute/simd.h"
#include "column/compute/add_tensor.h"
#include "column/compute/sub_tensor.h"
#include "column/compute/mul_tensor.h"
#include "column/compute/div_tensor.h"
#include "column/compute/scalar_tensor.h"
#include "column/compute/reduce_tensor.h"
#include "column/compute/compare_tensor.h"
#include "column/compute/unary_tensor.h"

// Legacy business nodes (Layer 1, sequential pipeline)
#include "column/node/node.h"
#include "column/node/transform_node.h"
#include "column/node/filter_node.h"
#include "column/node/aggregate_node.h"
#include "column/graph.h"

// Monitoring
#include "column/monitor.h"

// Execution layer
#include "column/exec/tensor_op.h"
#include "column/exec/exec_node.h"
#include "column/exec/compute_graph.h"

// Backend factory
#include "column/backend/backend.h"

// Execution context (feed / compute / fetch phases)
#include "column/context/execution_context.h"

// Business layer
#include "column/biz/sub_graph.h"
#include "column/biz/dsl.h"
#include "column/biz/module.h"
#include "column/biz/pipeline.h"

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
constexpr int kVersionMajor = 4;
constexpr int kVersionMinor = 0;
constexpr int kVersionPatch = 0;

}  // namespace lark::column
