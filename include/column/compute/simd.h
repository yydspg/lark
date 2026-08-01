// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Platform SIMD detection — inspired by ggml's approach.
//
// Defines exactly one of:
//   LARK_SIMD_NEON     — ARM64 NEON (128-bit, 2×f64 or 4×f32)
//   LARK_SIMD_AVX512   — x86 AVX-512 (512-bit, 8×f64 or 16×f32)
//   LARK_SIMD_AVX2     — x86 AVX2+FMA (256-bit, 4×f64 or 8×f32)
//   LARK_SIMD_SSE2     — x86 SSE2 (128-bit, 2×f64 or 4×f32)
//   LARK_SIMD_SCALAR   — fallback (no SIMD)
//
// Priority: AVX-512 > AVX2 > SSE2  (x86)  /  NEON  (ARM)
// ─────────────────────────────────────────────────────────────────────────────

// ── ARM NEON ─────────────────────────────────────────────────────────────────
#if defined(__aarch64__) || defined(_M_ARM64)
#  include <arm_neon.h>
#  define LARK_SIMD_NEON 1
#  define LARK_SIMD_NAME "NEON"

// ── x86 SIMD ─────────────────────────────────────────────────────────────────
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

#  if defined(__AVX512F__)
#    include <immintrin.h>
#    define LARK_SIMD_AVX512 1
#    define LARK_SIMD_NAME "AVX-512"

#  elif defined(__AVX2__)
#    include <immintrin.h>
#    define LARK_SIMD_AVX2 1
#    define LARK_SIMD_NAME "AVX2"

#  elif defined(__SSE2__)
#    include <emmintrin.h>
#    define LARK_SIMD_SSE2 1
#    define LARK_SIMD_NAME "SSE2"

#  else
#    define LARK_SIMD_SCALAR 1
#    define LARK_SIMD_NAME "Scalar"
#  endif

// ── Unknown platform ─────────────────────────────────────────────────────────
#else
#  define LARK_SIMD_SCALAR 1
#  define LARK_SIMD_NAME "Scalar"
#endif

// Ensure undefined macros are 0
#ifndef LARK_SIMD_NEON
#  define LARK_SIMD_NEON 0
#endif
#ifndef LARK_SIMD_AVX512
#  define LARK_SIMD_AVX512 0
#endif
#ifndef LARK_SIMD_AVX2
#  define LARK_SIMD_AVX2 0
#endif
#ifndef LARK_SIMD_SSE2
#  define LARK_SIMD_SSE2 0
#endif
#ifndef LARK_SIMD_SCALAR
#  define LARK_SIMD_SCALAR 0
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lark::column::simd {

// ── Runtime info ─────────────────────────────────────────────────────────────
inline const char* backend_name() { return LARK_SIMD_NAME; }

// ── Horizontal sum helpers (used by reduce kernels) ─────────────────────────

#if LARK_SIMD_NEON
inline double hsum_f64(float64x2_t v) {
  return vaddvq_f64(v);
}
#endif

#if LARK_SIMD_SSE2
inline double hsum_f64(__m128d v) {
  double tmp[2];
  _mm_storeu_pd(tmp, v);
  return tmp[0] + tmp[1];
}
#endif

#if LARK_SIMD_AVX2
inline double hsum_f64(__m256d v) {
  // v = [a, b, c, d]
  __m128d lo = _mm256_castpd256_pd128(v);       // [a, b]
  __m128d hi = _mm256_extractf128_pd(v, 1);     // [c, d]
  __m128d s  = _mm_add_pd(lo, hi);               // [a+c, b+d]
  double tmp[2];
  _mm_storeu_pd(tmp, s);
  return tmp[0] + tmp[1];
}
#endif

#if LARK_SIMD_AVX512
inline double hsum_f64(__m512d v) {
  // _mm512_reduce_add_pd available with AVX-512
  return _mm512_reduce_add_pd(v);
}
#endif

}  // namespace lark::column::simd
