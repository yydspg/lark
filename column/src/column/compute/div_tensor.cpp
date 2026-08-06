// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/compute/div_tensor.h"
#include "column/compute/simd.h"

#include <stdexcept>

namespace lark::column::compute {

Tensor div(const Tensor& a, const Tensor& b) {
  if (a.dtype() != b.dtype()) throw std::runtime_error("Type mismatch in div");
  const size_t n = a.size();

  // int64 / int64 → double
  if (a.dtype() == DType::kInt64) {
    Tensor result(DType::kDouble, n);
    const auto* pa = a.data_as<int64_t>();
    const auto* pb = b.data_as<int64_t>();
    auto* pr = result.data_as<double>();
    for (size_t i = 0; i < n; ++i)
      pr[i] = static_cast<double>(pa[i]) / static_cast<double>(pb[i]);
    result.resize(n);
    return result;
  }

  // double / double → double (SIMD)
  Tensor result(DType::kDouble, n);
  const auto* pa = a.data_as<double>();
  const auto* pb = b.data_as<double>();
  auto* pr = result.data_as<double>();

#if LARK_SIMD_NEON
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    float64x2_t va = vld1q_f64(pa + i);
    float64x2_t vb = vld1q_f64(pb + i);
    vst1q_f64(pr + i, vdivq_f64(va, vb));
  }
  for (; i < n; ++i) pr[i] = pa[i] / pb[i];

#elif LARK_SIMD_AVX512
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m512d va = _mm512_loadu_pd(pa + i);
    __m512d vb = _mm512_loadu_pd(pb + i);
    _mm512_storeu_pd(pr + i, _mm512_div_pd(va, vb));
  }
  for (; i < n; ++i) pr[i] = pa[i] / pb[i];

#elif LARK_SIMD_AVX2
  size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m256d va = _mm256_loadu_pd(pa + i);
    __m256d vb = _mm256_loadu_pd(pb + i);
    _mm256_storeu_pd(pr + i, _mm256_div_pd(va, vb));
  }
  for (; i < n; ++i) pr[i] = pa[i] / pb[i];

#elif LARK_SIMD_SSE2
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    __m128d va = _mm_loadu_pd(pa + i);
    __m128d vb = _mm_loadu_pd(pb + i);
    _mm_storeu_pd(pr + i, _mm_div_pd(va, vb));
  }
  for (; i < n; ++i) pr[i] = pa[i] / pb[i];

#else
  for (size_t i = 0; i < n; ++i) pr[i] = pa[i] / pb[i];
#endif

  result.resize(n);
  return result;
}

}  // namespace lark::column::compute
