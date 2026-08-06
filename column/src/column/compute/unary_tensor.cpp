// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/compute/unary_tensor.h"
#include "column/compute/simd.h"

#include <cmath>

namespace lark::column::compute {

// ── neg ──────────────────────────────────────────────────────────────────────
Tensor neg(const Tensor& a) {
  const size_t n = a.size();
  Tensor result(a.dtype(), n);

  if (a.dtype() == DType::kInt64) {
    const auto* pa = a.data_as<int64_t>();
    auto* pr = result.data_as<int64_t>();
#pragma clang loop vectorize(enable) interleave(enable)
    for (size_t i = 0; i < n; ++i) pr[i] = -pa[i];
    result.resize(n);
    return result;
  }

  // float64 SIMD
  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<double>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    vst1q_f64(pr + i, vnegq_f64(va));
  }
  for (; i < n; ++i) pr[i] = -pa[i];

#elif LARK_SIMD_AVX512
  // Negate via XOR with sign bit or subtract from zero
  __m512d zero = _mm512_setzero_pd();
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m512d va = _mm512_loadu_pd(pa + i);
    _mm512_storeu_pd(pr + i, _mm512_sub_pd(zero, va));
  }
  for (; i < n; ++i) pr[i] = -pa[i];

#elif LARK_SIMD_AVX2
  __m256d zero = _mm256_setzero_pd();
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m256d va = _mm256_loadu_pd(pa + i);
    _mm256_storeu_pd(pr + i, _mm256_sub_pd(zero, va));
  }
  for (; i < n; ++i) pr[i] = -pa[i];

#elif LARK_SIMD_SSE2
  __m128d zero = _mm_setzero_pd();
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    __m128d va = _mm_loadu_pd(pa + i);
    _mm_storeu_pd(pr + i, _mm_sub_pd(zero, va));
  }
  for (; i < n; ++i) pr[i] = -pa[i];

#else
  for (size_t i = 0; i < n; ++i) pr[i] = -pa[i];
#endif

  result.resize(n);
  return result;
}

// ── abs ──────────────────────────────────────────────────────────────────────
Tensor abs(const Tensor& a) {
  const size_t n = a.size();
  Tensor result(a.dtype(), n);

  if (a.dtype() == DType::kInt64) {
    const auto* pa = a.data_as<int64_t>();
    auto* pr = result.data_as<int64_t>();
#pragma clang loop vectorize(enable) interleave(enable)
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] < 0 ? -pa[i] : pa[i];
    result.resize(n);
    return result;
  }

  // float64 SIMD
  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<double>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    vst1q_f64(pr + i, vabsq_f64(va));
  }
  for (; i < n; ++i) pr[i] = std::fabs(pa[i]);

#elif LARK_SIMD_AVX512
  // AVX-512: clear sign bit via AND with abs mask
  __m512d sign_mask = _mm512_castsi512_pd(
      _mm512_set1_epi64(0x7FFFFFFFFFFFFFFFLL));
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m512d va = _mm512_loadu_pd(pa + i);
    _mm512_storeu_pd(pr + i, _mm512_and_pd(va, sign_mask));
  }
  for (; i < n; ++i) pr[i] = std::fabs(pa[i]);

#elif LARK_SIMD_AVX2
  __m256d sign_mask = _mm256_castsi256_pd(
      _mm256_set1_epi64x(0x7FFFFFFFFFFFFFFFLL));
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m256d va = _mm256_loadu_pd(pa + i);
    _mm256_storeu_pd(pr + i, _mm256_and_pd(va, sign_mask));
  }
  for (; i < n; ++i) pr[i] = std::fabs(pa[i]);

#elif LARK_SIMD_SSE2
  __m128d sign_mask = _mm_castsi128_pd(
      _mm_set1_epi64x(0x7FFFFFFFFFFFFFFFLL));
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    __m128d va = _mm_loadu_pd(pa + i);
    _mm_storeu_pd(pr + i, _mm_and_pd(va, sign_mask));
  }
  for (; i < n; ++i) pr[i] = std::fabs(pa[i]);

#else
  for (size_t i = 0; i < n; ++i) pr[i] = std::fabs(pa[i]);
#endif

  result.resize(n);
  return result;
}

// ── sqrt (always returns double) ─────────────────────────────────────────────
Tensor sqrt(const Tensor& a) {
  const size_t n = a.size();
  Tensor result(DType::kDouble, n);
  auto* pr = result.data_as<double>();

  if (a.dtype() == DType::kInt64) {
    const auto* pa = a.data_as<int64_t>();
    for (size_t i = 0; i < n; ++i)
      pr[i] = std::sqrt(static_cast<double>(pa[i]));
    result.resize(n);
    return result;
  }

  // float64 SIMD
  const auto* pa = a.data_as<double>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    vst1q_f64(pr + i, vsqrtq_f64(va));
  }
  for (; i < n; ++i) pr[i] = std::sqrt(pa[i]);

#elif LARK_SIMD_AVX512
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m512d va = _mm512_loadu_pd(pa + i);
    _mm512_storeu_pd(pr + i, _mm512_sqrt_pd(va));
  }
  for (; i < n; ++i) pr[i] = std::sqrt(pa[i]);

#elif LARK_SIMD_AVX2
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m256d va = _mm256_loadu_pd(pa + i);
    _mm256_storeu_pd(pr + i, _mm256_sqrt_pd(va));
  }
  for (; i < n; ++i) pr[i] = std::sqrt(pa[i]);

#elif LARK_SIMD_SSE2
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    __m128d va = _mm_loadu_pd(pa + i);
    _mm_storeu_pd(pr + i, _mm_sqrt_pd(va));
  }
  for (; i < n; ++i) pr[i] = std::sqrt(pa[i]);

#else
  for (size_t i = 0; i < n; ++i) pr[i] = std::sqrt(pa[i]);
#endif

  result.resize(n);
  return result;
}

// ── square ───────────────────────────────────────────────────────────────────
Tensor square(const Tensor& a) {
  const size_t n = a.size();
  Tensor result(a.dtype(), n);

  if (a.dtype() == DType::kInt64) {
    const auto* pa = a.data_as<int64_t>();
    auto* pr = result.data_as<int64_t>();
#pragma clang loop vectorize(enable) interleave(enable)
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] * pa[i];
    result.resize(n);
    return result;
  }

  // float64 SIMD: square = a * a
  const auto* pa = a.data_as<double>();
  auto* pr = result.data_as<double>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    vst1q_f64(pr + i, vmulq_f64(va, va));
  }
  for (; i < n; ++i) pr[i] = pa[i] * pa[i];

#elif LARK_SIMD_AVX512
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m512d va = _mm512_loadu_pd(pa + i);
    _mm512_storeu_pd(pr + i, _mm512_mul_pd(va, va));
  }
  for (; i < n; ++i) pr[i] = pa[i] * pa[i];

#elif LARK_SIMD_AVX2
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m256d va = _mm256_loadu_pd(pa + i);
    _mm256_storeu_pd(pr + i, _mm256_mul_pd(va, va));
  }
  for (; i < n; ++i) pr[i] = pa[i] * pa[i];

#elif LARK_SIMD_SSE2
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    __m128d va = _mm_loadu_pd(pa + i);
    _mm_storeu_pd(pr + i, _mm_mul_pd(va, va));
  }
  for (; i < n; ++i) pr[i] = pa[i] * pa[i];

#else
  for (size_t i = 0; i < n; ++i) pr[i] = pa[i] * pa[i];
#endif

  result.resize(n);
  return result;
}

}  // namespace lark::column::compute
