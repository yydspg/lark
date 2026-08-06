// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/compute/add_tensor.h"
#include "column/compute/simd.h"

#include <stdexcept>

namespace lark::column::compute {

// ── int64 path: scalar with vectorize hint ───────────────────────────────────
static Tensor add_int64(const Tensor& a, const Tensor& b) {
  const size_t n = a.size();
  Tensor result(a.dtype(), n);
  const auto* pa = a.data_as<int64_t>();
  const auto* pb = b.data_as<int64_t>();
  auto* pr = result.data_as<int64_t>();
#pragma clang loop vectorize(enable) interleave(enable)
  for (size_t i = 0; i < n; ++i) pr[i] = pa[i] + pb[i];
  result.resize(n);
  return result;
}

// ── float64 path: explicit SIMD intrinsics ───────────────────────────────────
static Tensor add_f64(const Tensor& a, const Tensor& b) {
  const size_t n = a.size();
  Tensor result(a.dtype(), n);
  const auto* pa = a.data_as<double>();
  const auto* pb = b.data_as<double>();
  auto* pr = result.data_as<double>();

#if LARK_SIMD_NEON
  // NEON: 2 doubles per register
  const size_t stride = 2;
  size_t i = 0;
  for (; i + stride <= n; i += stride) {
    float64x2_t va = vld1q_f64(pa + i);
    float64x2_t vb = vld1q_f64(pb + i);
    vst1q_f64(pr + i, vaddq_f64(va, vb));
  }
  for (; i < n; ++i) pr[i] = pa[i] + pb[i];

#elif LARK_SIMD_AVX512
  // AVX-512: 8 doubles per register
  const size_t stride = 8;
  size_t i = 0;
  for (; i + stride <= n; i += stride) {
    __m512d va = _mm512_loadu_pd(pa + i);
    __m512d vb = _mm512_loadu_pd(pb + i);
    _mm512_storeu_pd(pr + i, _mm512_add_pd(va, vb));
  }
  for (; i < n; ++i) pr[i] = pa[i] + pb[i];

#elif LARK_SIMD_AVX2
  // AVX2: 4 doubles per register
  const size_t stride = 4;
  size_t i = 0;
  for (; i + stride <= n; i += stride) {
    __m256d va = _mm256_loadu_pd(pa + i);
    __m256d vb = _mm256_loadu_pd(pb + i);
    _mm256_storeu_pd(pr + i, _mm256_add_pd(va, vb));
  }
  for (; i < n; ++i) pr[i] = pa[i] + pb[i];

#elif LARK_SIMD_SSE2
  // SSE2: 2 doubles per register
  const size_t stride = 2;
  size_t i = 0;
  for (; i + stride <= n; i += stride) {
    __m128d va = _mm_loadu_pd(pa + i);
    __m128d vb = _mm_loadu_pd(pb + i);
    _mm_storeu_pd(pr + i, _mm_add_pd(va, vb));
  }
  for (; i < n; ++i) pr[i] = pa[i] + pb[i];

#else
  // Scalar fallback
  for (size_t i = 0; i < n; ++i) pr[i] = pa[i] + pb[i];
#endif

  result.resize(n);
  return result;
}

// ── Dispatch ─────────────────────────────────────────────────────────────────
Tensor add(const Tensor& a, const Tensor& b) {
  if (a.dtype() != b.dtype()) throw std::runtime_error("Type mismatch in add");
  return a.dtype() == DType::kInt64 ? add_int64(a, b) : add_f64(a, b);
}

}  // namespace lark::column::compute
