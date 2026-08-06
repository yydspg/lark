// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// DType: numeric element type.
//
// Supports the four scalar numeric types used by the engine plus one
// ggml-style block-quantized type:
//   kInt32   — 32-bit signed integer
//   kInt64   — 64-bit signed integer
//   kFloat32 — single-precision float
//   kFloat64 — double-precision float (alias kDouble for backward compat)
//   kQ8_0    — 8-bit block quantization: 32 values per block, one float scale
//              per block (inspired by ggml's Q8_0 layout).
// ─────────────────────────────────────────────────────────────────────────────
enum class DType {
  kInt32,
  kInt64,
  kFloat32,
  kFloat64,
  kDouble = kFloat64,  // legacy alias
  kQ8_0,
};

inline bool is_float(DType t) noexcept {
  return t == DType::kFloat32 || t == DType::kFloat64;
}
inline bool is_int(DType t) noexcept {
  return t == DType::kInt32 || t == DType::kInt64;
}
inline bool is_scalar_type(DType t) noexcept {
  return is_float(t) || is_int(t);
}
inline bool is_quantized(DType t) noexcept { return t == DType::kQ8_0; }

// ggml-style Q8_0 block: `qs` holds 32 int8 values; `d` is the per-block
// scale. 1.5 KB of quantized data per 1000 elements (vs 8 KB for float64).
constexpr size_t kQ8BlockSize = 32;
struct BlockQ8_0 {
  float d;
  int8_t qs[kQ8BlockSize];
};
static_assert(sizeof(BlockQ8_0) == 36, "BlockQ8_0 must be 36 bytes");

// Byte width of a single scalar element (or a single block for kQ8_0).
inline size_t dtype_size(DType t) noexcept {
  switch (t) {
    case DType::kInt32:
      return 4;
    case DType::kInt64:
      return 8;
    case DType::kFloat32:
      return 4;
    case DType::kFloat64:
      return 8;
    case DType::kQ8_0:
      return sizeof(BlockQ8_0);
  }
  return 0;
}

inline const char* dtype_name(DType t) noexcept {
  switch (t) {
    case DType::kInt32:
      return "int32";
    case DType::kInt64:
      return "int64";
    case DType::kFloat32:
      return "float32";
    case DType::kFloat64:
      return "float64";
    case DType::kQ8_0:
      return "q8_0";
  }
  return "unknown";
}

// Bytes needed to store `count` logical elements of `dtype`.
// Quantized types round up to whole blocks.
inline size_t dtype_bytes(size_t count, DType t) noexcept {
  if (t == DType::kQ8_0) {
    return (count + kQ8BlockSize - 1) / kQ8BlockSize * sizeof(BlockQ8_0);
  }
  return count * dtype_size(t);
}

inline DType parse_dtype(const std::string& s) {
  if (s == "int32") return DType::kInt32;
  if (s == "int64") return DType::kInt64;
  if (s == "float32" || s == "float") return DType::kFloat32;
  if (s == "float64" || s == "double") return DType::kFloat64;
  throw std::runtime_error("unknown dtype: " + s);
}

}  // namespace lark::column
