// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "column/types.h"

namespace lark::column {

// Tensor element type — see column/types.h for the full DType set.
// (DType, dtype_size, BlockQ8_0, ... are defined there.)

// ─────────────────────────────────────────────────────────────────────────────
// Tensor: 1-D numeric array — pure data container.
//
// All arithmetic operations live in compute/ (or in the exec/ op layer) as
// free functions / ops. The Tensor itself only manages memory, element
// access, and metadata.
//
// For quantized tensors (DType::kQ8_0) the underlying buffer holds whole
// BlockQ8_0 blocks (32 values each) and size() reports the logical element
// count.
// ─────────────────────────────────────────────────────────────────────────────
class Tensor {
 public:
  // ── Factory helpers ────────────────────────────────────────────────────
  static Tensor zeros(size_t n, DType dtype);
  static Tensor ones(size_t n, DType dtype);
  static Tensor full(size_t n, double value, DType dtype);
  static Tensor from_data(std::vector<int32_t> data);
  static Tensor from_data(std::vector<int64_t> data);
  static Tensor from_data(std::vector<float> data);
  static Tensor from_data(std::vector<double> data);

  // ── Construction ───────────────────────────────────────────────────────
  Tensor() = default;
  explicit Tensor(DType dtype, size_t capacity = 0);
  Tensor(std::initializer_list<int64_t> il);
  Tensor(std::initializer_list<double> il);

  // Move-only (use clone() for deep copy)
  Tensor(Tensor&&) noexcept = default;
  Tensor& operator=(Tensor&&) noexcept = default;
  Tensor(const Tensor&) = delete;
  Tensor& operator=(const Tensor&) = delete;

  // ── Element access ─────────────────────────────────────────────────────
  template <typename T>
  T get(size_t idx) const {
    if (idx >= size_) throw std::out_of_range("Tensor index out of range");
    return reinterpret_cast<const T*>(data_.data())[idx];
  }

  template <typename T>
  void set(size_t idx, T value) {
    if (idx >= size_) throw std::out_of_range("Tensor index out of range");
    reinterpret_cast<T*>(data_.data())[idx] = value;
  }

  void append_int64(int64_t v);
  void append_double(double v);

  // ── Properties ─────────────────────────────────────────────────────────
  DType dtype() const noexcept { return dtype_; }
  size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  // Number of storage blocks (meaningful for quantized tensors only).
  size_t num_blocks() const noexcept {
    if (dtype_ == DType::kQ8_0) {
      return (size_ + kQ8BlockSize - 1) / kQ8BlockSize;
    }
    return size_;
  }

  // ── Quantization (ggml-style Q8_0) ─────────────────────────────────────
  // Quantize a float tensor to 8-bit blocks with per-block scale.
  // Result keeps size() == input size() but dtype() == kQ8_0.
  Tensor quantize_q8_0() const;
  // Dequantize a kQ8_0 tensor back to float64. Throws if not quantized.
  Tensor dequantize() const;
  // Block-quantized dot product (both tensors must be kQ8_0, same length).
  double dot_q8_0(const Tensor& other) const;

  // ── Raw data access (for compute kernels / SIMD) ───────────────────────
  const uint8_t* data() const noexcept { return data_.data(); }
  uint8_t* mutable_data() noexcept { return data_.data(); }

  template <typename T>
  const T* data_as() const noexcept {
    return reinterpret_cast<const T*>(data_.data());
  }
  template <typename T>
  T* data_as() noexcept {
    return reinterpret_cast<T*>(data_.data());
  }

  // ── Utility ────────────────────────────────────────────────────────────
  Tensor clone() const;
  Tensor to_double() const;  // int64 → double conversion
  void reserve(size_t cap);
  void resize(size_t n);
  void clear() noexcept { size_ = 0; }

 private:
  void ensure_capacity(size_t required);

  DType dtype_ = DType::kDouble;
  std::vector<uint8_t> data_;
  size_t size_ = 0;
  size_t capacity_ = 0;
};

}  // namespace lark::column
