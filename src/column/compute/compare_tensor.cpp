// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/compute/compare_tensor.h"
#include "column/compute/simd.h"

namespace lark::column::compute {

// ── Helpers ──────────────────────────────────────────────────────────────────
//
// Comparison results are int64 masks (1 = true, 0 = false).
// NEON/SSE compare instructions produce all-ones or all-zeros per element,
// so we AND with 1 to normalize to {0, 1}.

namespace {

// ── int64 tensor-tensor compare ──────────────────────────────────────────────
template <typename Op>
Tensor cmp_int64_tt(const Tensor& a, const Tensor& b, Op op) {
  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<int64_t>();
  const auto* pb = b.data_as<int64_t>();
  auto* pr = result.data_as<int64_t>();
#pragma clang loop vectorize(enable) interleave(enable)
  for (size_t i = 0; i < n; ++i) pr[i] = op(pa[i], pb[i]) ? 1 : 0;
  result.resize(n);
  return result;
}

// ── double tensor-tensor compare: SIMD ───────────────────────────────────────
template <typename VecCmp>
Tensor cmp_f64_tt_simd(const Tensor& a, const Tensor& b, VecCmp cmp_fn) {
  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  const auto* pb = b.data_as<double>();
  auto* pr = result.data_as<int64_t>();
  return cmp_fn(pa, pb, pr, n);
}

// ── int64 tensor-scalar compare ──────────────────────────────────────────────
template <typename Op>
Tensor cmp_int64_ts(const Tensor& a, double sv, Op op) {
  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<int64_t>();
  auto* pr = result.data_as<int64_t>();
  int64_t s = static_cast<int64_t>(sv);
#pragma clang loop vectorize(enable) interleave(enable)
  for (size_t i = 0; i < n; ++i) pr[i] = op(pa[i], s) ? 1 : 0;
  result.resize(n);
  return result;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Tensor-tensor comparisons
// ─────────────────────────────────────────────────────────────────────────────

Tensor gt(const Tensor& a, const Tensor& b) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_tt(a, b, [](int64_t x, int64_t y) { return x > y; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  const auto* pb = b.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    float64x2_t vb = vld1q_f64(pb + i);
    uint64x2_t mask = vcgtq_f64(va, vb);
    // Convert mask to int64 {0,1}: AND with 1
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] > pb[i]) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] > pb[i]) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

Tensor lt(const Tensor& a, const Tensor& b) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_tt(a, b, [](int64_t x, int64_t y) { return x < y; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  const auto* pb = b.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    float64x2_t vb = vld1q_f64(pb + i);
    uint64x2_t mask = vcltq_f64(va, vb);
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] < pb[i]) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] < pb[i]) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

Tensor ge(const Tensor& a, const Tensor& b) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_tt(a, b, [](int64_t x, int64_t y) { return x >= y; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  const auto* pb = b.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    float64x2_t vb = vld1q_f64(pb + i);
    uint64x2_t mask = vcgeq_f64(va, vb);
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] >= pb[i]) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] >= pb[i]) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

Tensor le(const Tensor& a, const Tensor& b) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_tt(a, b, [](int64_t x, int64_t y) { return x <= y; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  const auto* pb = b.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    float64x2_t vb = vld1q_f64(pb + i);
    uint64x2_t mask = vcleq_f64(va, vb);
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] <= pb[i]) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] <= pb[i]) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

Tensor eq(const Tensor& a, const Tensor& b) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_tt(a, b, [](int64_t x, int64_t y) { return x == y; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  const auto* pb = b.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    float64x2_t vb = vld1q_f64(pb + i);
    uint64x2_t mask = vceqq_f64(va, vb);
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] == pb[i]) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] == pb[i]) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scalar comparisons
// ─────────────────────────────────────────────────────────────────────────────

Tensor gt_scalar(const Tensor& a, double v) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_ts(a, v, [](int64_t x, int64_t s) { return x > s; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  float64x2_t vs = vdupq_n_f64(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    uint64x2_t mask = vcgtq_f64(va, vs);
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] > v) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] > v) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

Tensor lt_scalar(const Tensor& a, double v) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_ts(a, v, [](int64_t x, int64_t s) { return x < s; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  float64x2_t vs = vdupq_n_f64(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    uint64x2_t mask = vcltq_f64(va, vs);
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] < v) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] < v) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

Tensor ge_scalar(const Tensor& a, double v) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_ts(a, v, [](int64_t x, int64_t s) { return x >= s; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  float64x2_t vs = vdupq_n_f64(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    uint64x2_t mask = vcgeq_f64(va, vs);
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] >= v) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] >= v) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

Tensor le_scalar(const Tensor& a, double v) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_ts(a, v, [](int64_t x, int64_t s) { return x <= s; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  float64x2_t vs = vdupq_n_f64(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    uint64x2_t mask = vcleq_f64(va, vs);
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] <= v) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] <= v) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

Tensor eq_scalar(const Tensor& a, double v) {
  if (a.dtype() == DType::kInt64)
    return cmp_int64_ts(a, v, [](int64_t x, int64_t s) { return x == s; });

  const size_t n = a.size();
  Tensor result(DType::kInt64, n);
  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<int64_t>();

#if LARK_SIMD_NEON
  float64x2_t vs = vdupq_n_f64(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    uint64x2_t mask = vceqq_f64(va, vs);
    int64x2_t one = vdupq_n_s64(1);
    vst1q_s64(pr + i, vandq_s64(vreinterpretq_s64_u64(mask), one));
  }
  for (; i < n; ++i) pr[i] = (pa[i] == v) ? 1 : 0;
#else
  for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] == v) ? 1 : 0;
#endif

  result.resize(n);
  return result;
}

}  // namespace lark::column::compute
