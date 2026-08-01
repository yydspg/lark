// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/tensor.h"

#include <algorithm>
#include <cstring>

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// Construction helpers
// ─────────────────────────────────────────────────────────────────────────────

void Tensor::ensure_capacity(size_t required) {
  if (required <= capacity_) return;
  size_t new_cap = std::max(required, capacity_ * 2);
  data_.resize(new_cap * dtype_size(dtype_));
  capacity_ = new_cap;
}

Tensor::Tensor(DType dtype, size_t capacity) : dtype_(dtype) {
  if (capacity > 0) ensure_capacity(capacity);
}

Tensor::Tensor(std::initializer_list<int64_t> il) : dtype_(DType::kInt64) {
  ensure_capacity(il.size());
  std::memcpy(mutable_data(), il.begin(), il.size() * sizeof(int64_t));
  size_ = il.size();
}

Tensor::Tensor(std::initializer_list<double> il) : dtype_(DType::kDouble) {
  ensure_capacity(il.size());
  std::memcpy(mutable_data(), il.begin(), il.size() * sizeof(double));
  size_ = il.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory methods
// ─────────────────────────────────────────────────────────────────────────────

Tensor Tensor::zeros(size_t n, DType dtype) {
  Tensor t(dtype, n);
  t.data_.assign(n * dtype_size(dtype), 0);
  t.size_ = n;
  t.capacity_ = n;
  return t;
}

Tensor Tensor::ones(size_t n, DType dtype) {
  Tensor t(dtype, n);
  if (dtype == DType::kInt64) {
    auto* p = t.data_as<int64_t>();
    for (size_t i = 0; i < n; ++i) p[i] = 1;
  } else {
    auto* p = t.data_as<double>();
    for (size_t i = 0; i < n; ++i) p[i] = 1.0;
  }
  t.size_ = n;
  t.capacity_ = n;
  return t;
}

Tensor Tensor::full(size_t n, double value, DType dtype) {
  Tensor t(dtype, n);
  if (dtype == DType::kInt64) {
    auto v = static_cast<int64_t>(value);
    auto* p = t.data_as<int64_t>();
    for (size_t i = 0; i < n; ++i) p[i] = v;
  } else {
    auto* p = t.data_as<double>();
    for (size_t i = 0; i < n; ++i) p[i] = value;
  }
  t.size_ = n;
  t.capacity_ = n;
  return t;
}

Tensor Tensor::from_data(std::vector<int64_t> data) {
  Tensor t(DType::kInt64, data.size());
  std::memcpy(t.mutable_data(), data.data(), data.size() * sizeof(int64_t));
  t.size_ = data.size();
  t.capacity_ = data.size();
  return t;
}

Tensor Tensor::from_data(std::vector<double> data) {
  Tensor t(DType::kDouble, data.size());
  std::memcpy(t.mutable_data(), data.data(), data.size() * sizeof(double));
  t.size_ = data.size();
  t.capacity_ = data.size();
  return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Append
// ─────────────────────────────────────────────────────────────────────────────

void Tensor::append_int64(int64_t v) {
  if (dtype_ != DType::kInt64)
    throw std::runtime_error("append_int64 on non-int64 tensor");
  ensure_capacity(size_ + 1);
  data_as<int64_t>()[size_] = v;
  ++size_;
}

void Tensor::append_double(double v) {
  if (dtype_ != DType::kDouble)
    throw std::runtime_error("append_double on non-double tensor");
  ensure_capacity(size_ + 1);
  data_as<double>()[size_] = v;
  ++size_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────────────────────────────────────

Tensor Tensor::clone() const {
  Tensor t(dtype_, capacity_);
  t.size_ = size_;
  if (size_ > 0) {
    std::memcpy(t.mutable_data(), data_.data(), size_ * dtype_size(dtype_));
  }
  return t;
}

Tensor Tensor::to_double() const {
  if (dtype_ == DType::kDouble) return clone();
  const size_t n = size_;
  Tensor result(DType::kDouble, n);
  const auto* pa = data_as<int64_t>();
  auto* pr = result.data_as<double>();
  for (size_t i = 0; i < n; ++i) pr[i] = static_cast<double>(pa[i]);
  result.resize(n);
  return result;
}

void Tensor::reserve(size_t cap) { ensure_capacity(cap); }

void Tensor::resize(size_t n) {
  ensure_capacity(n);
  size_ = n;
}

}  // namespace lark::column
