// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/compute/reduce_tensor.h"
#include "column/compute/simd.h"

namespace lark::column::compute {

double sum(const Tensor& a) {
  const size_t n = a.size();

  // int64 path: scalar accumulation (avoid precision loss)
  if (a.dtype() == DType::kInt64) {
    const auto* p = a.data_as<int64_t>();
    int64_t s = 0;
    for (size_t i = 0; i < n; ++i) s += p[i];
    return static_cast<double>(s);
  }

  // float64 path: SIMD-accelerated sum
  const auto* p = a.data_as<double>();

#if LARK_SIMD_NEON
  float64x2_t acc = vdupq_n_f64(0.0);
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    acc = vaddq_f64(acc, vld1q_f64(p + i));
  }
  double s = simd::hsum_f64(acc);
  for (; i < n; ++i) s += p[i];
  return s;

#elif LARK_SIMD_AVX512
  __m512d acc = _mm512_setzero_pd();
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    acc = _mm512_add_pd(acc, _mm512_loadu_pd(p + i));
  }
  double s = simd::hsum_f64(acc);
  for (; i < n; ++i) s += p[i];
  return s;

#elif LARK_SIMD_AVX2
  __m256d acc = _mm256_setzero_pd();
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    acc = _mm256_add_pd(acc, _mm256_loadu_pd(p + i));
  }
  double s = simd::hsum_f64(acc);
  for (; i < n; ++i) s += p[i];
  return s;

#elif LARK_SIMD_SSE2
  __m128d acc = _mm_setzero_pd();
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    acc = _mm_add_pd(acc, _mm_loadu_pd(p + i));
  }
  double s = simd::hsum_f64(acc);
  for (; i < n; ++i) s += p[i];
  return s;

#else
  double s = 0.0;
  for (size_t i = 0; i < n; ++i) s += p[i];
  return s;
#endif
}

double mean(const Tensor& a) {
  if (a.size() == 0) return 0.0;
  return sum(a) / static_cast<double>(a.size());
}

int64_t count(const Tensor& a) { return static_cast<int64_t>(a.size()); }

}  // namespace lark::column::compute
