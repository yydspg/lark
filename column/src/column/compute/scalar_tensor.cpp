// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/compute/scalar_tensor.h"
#include "column/compute/simd.h"

namespace lark::column::compute {

// ── int64 scalar helpers (scalar code, compiler auto-vectorizes) ─────────────

static Tensor scalar_int64(const Tensor& a, double sv,
                            int64_t (*op)(int64_t, int64_t)) {
  const size_t n = a.size();
  Tensor result(a.dtype(), n);
  const auto* pa = a.data_as<int64_t>();
  auto* pr = result.data_as<int64_t>();
  int64_t s = static_cast<int64_t>(sv);
#pragma clang loop vectorize(enable) interleave(enable)
  for (size_t i = 0; i < n; ++i) pr[i] = op(pa[i], s);
  result.resize(n);
  return result;
}

// ── float64 scalar ops: explicit SIMD ────────────────────────────────────────

Tensor add_scalar(const Tensor& a, double v) {
  const size_t n = a.size();
  Tensor result(a.dtype(), n);

  if (a.dtype() == DType::kInt64) {
    return scalar_int64(a, v, [](int64_t x, int64_t s) { return x + s; });
  }

  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<double>();

#if LARK_SIMD_NEON
  float64x2_t vs = vdupq_n_f64(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    vst1q_f64(pr + i, vaddq_f64(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] + v;

#elif LARK_SIMD_AVX512
  __m512d vs = _mm512_set1_pd(v);
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m512d va = _mm512_loadu_pd(pa + i);
    _mm512_storeu_pd(pr + i, _mm512_add_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] + v;

#elif LARK_SIMD_AVX2
  __m256d vs = _mm256_set1_pd(v);
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m256d va = _mm256_loadu_pd(pa + i);
    _mm256_storeu_pd(pr + i, _mm256_add_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] + v;

#elif LARK_SIMD_SSE2
  __m128d vs = _mm_set1_pd(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    __m128d va = _mm_loadu_pd(pa + i);
    _mm_storeu_pd(pr + i, _mm_add_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] + v;

#else
  for (size_t i = 0; i < n; ++i) pr[i] = pa[i] + v;
#endif

  result.resize(n);
  return result;
}

Tensor sub_scalar(const Tensor& a, double v) {
  const size_t n = a.size();
  Tensor result(a.dtype(), n);

  if (a.dtype() == DType::kInt64) {
    return scalar_int64(a, v, [](int64_t x, int64_t s) { return x - s; });
  }

  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<double>();

#if LARK_SIMD_NEON
  float64x2_t vs = vdupq_n_f64(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    vst1q_f64(pr + i, vsubq_f64(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] - v;

#elif LARK_SIMD_AVX512
  __m512d vs = _mm512_set1_pd(v);
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m512d va = _mm512_loadu_pd(pa + i);
    _mm512_storeu_pd(pr + i, _mm512_sub_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] - v;

#elif LARK_SIMD_AVX2
  __m256d vs = _mm256_set1_pd(v);
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m256d va = _mm256_loadu_pd(pa + i);
    _mm256_storeu_pd(pr + i, _mm256_sub_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] - v;

#elif LARK_SIMD_SSE2
  __m128d vs = _mm_set1_pd(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    __m128d va = _mm_loadu_pd(pa + i);
    _mm_storeu_pd(pr + i, _mm_sub_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] - v;

#else
  for (size_t i = 0; i < n; ++i) pr[i] = pa[i] - v;
#endif

  result.resize(n);
  return result;
}

Tensor mul_scalar(const Tensor& a, double v) {
  const size_t n = a.size();
  Tensor result(a.dtype(), n);

  if (a.dtype() == DType::kInt64) {
    return scalar_int64(a, v, [](int64_t x, int64_t s) { return x * s; });
  }

  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<double>();

#if LARK_SIMD_NEON
  float64x2_t vs = vdupq_n_f64(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    vst1q_f64(pr + i, vmulq_f64(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] * v;

#elif LARK_SIMD_AVX512
  __m512d vs = _mm512_set1_pd(v);
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m512d va = _mm512_loadu_pd(pa + i);
    _mm512_storeu_pd(pr + i, _mm512_mul_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] * v;

#elif LARK_SIMD_AVX2
  __m256d vs = _mm256_set1_pd(v);
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m256d va = _mm256_loadu_pd(pa + i);
    _mm256_storeu_pd(pr + i, _mm256_mul_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] * v;

#elif LARK_SIMD_SSE2
  __m128d vs = _mm_set1_pd(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    __m128d va = _mm_loadu_pd(pa + i);
    _mm_storeu_pd(pr + i, _mm_mul_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] * v;

#else
  for (size_t i = 0; i < n; ++i) pr[i] = pa[i] * v;
#endif

  result.resize(n);
  return result;
}

Tensor div_scalar(const Tensor& a, double v) {
  const size_t n = a.size();

  // int64 / scalar → double result
  if (a.dtype() == DType::kInt64) {
    Tensor result(DType::kDouble, n);
    const auto* pa = a.data_as<int64_t>();
    auto* pr = result.data_as<double>();
    double s = v;
#pragma clang loop vectorize(enable) interleave(enable)
    for (size_t i = 0; i < n; ++i)
      pr[i] = static_cast<double>(pa[i]) / s;
    result.resize(n);
    return result;
  }

  // double / scalar (SIMD)
  Tensor result(DType::kDouble, n);
  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<double>();

#if LARK_SIMD_NEON
  float64x2_t vs = vdupq_n_f64(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    vst1q_f64(pr + i, vdivq_f64(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] / v;

#elif LARK_SIMD_AVX512
  __m512d vs = _mm512_set1_pd(v);
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m512d va = _mm512_loadu_pd(pa + i);
    _mm512_storeu_pd(pr + i, _mm512_div_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] / v;

#elif LARK_SIMD_AVX2
  __m256d vs = _mm256_set1_pd(v);
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m256d va = _mm256_loadu_pd(pa + i);
    _mm256_storeu_pd(pr + i, _mm256_div_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] / v;

#elif LARK_SIMD_SSE2
  __m128d vs = _mm_set1_pd(v);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    __m128d va = _mm_loadu_pd(pa + i);
    _mm_storeu_pd(pr + i, _mm_div_pd(va, vs));
  }
  for (; i < n; ++i) pr[i] = pa[i] / v;

#else
  for (size_t i = 0; i < n; ++i) pr[i] = pa[i] / v;
#endif

  result.resize(n);
  return result;
}

}  // namespace lark::column::compute
