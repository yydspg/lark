// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/tensor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// Storage helpers
// ─────────────────────────────────────────────────────────────────────────────

void Tensor::ensure_capacity(size_t required) {
  if (required <= capacity_) return;
  size_t new_cap = std::max(required, capacity_ * 2);
  data_.resize(dtype_bytes(new_cap, dtype_));
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

Tensor::Tensor(std::initializer_list<double> il) : dtype_(DType::kFloat64) {
  ensure_capacity(il.size());
  std::memcpy(mutable_data(), il.begin(), il.size() * sizeof(double));
  size_ = il.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory methods
// ─────────────────────────────────────────────────────────────────────────────

Tensor Tensor::zeros(size_t n, DType dtype) {
  Tensor t(dtype, n);
  t.data_.assign(dtype_bytes(n, dtype), 0);
  t.size_ = n;
  t.capacity_ = n;
  return t;
}

Tensor Tensor::ones(size_t n, DType dtype) {
  Tensor t = full(n, 1.0, dtype);
  return t;
}

Tensor Tensor::full(size_t n, double value, DType dtype) {
  Tensor t(dtype, n);
  switch (dtype) {
    case DType::kInt32: {
      auto* p = t.data_as<int32_t>();
      int32_t v = static_cast<int32_t>(value);
      for (size_t i = 0; i < n; ++i) p[i] = v;
      break;
    }
    case DType::kInt64: {
      auto* p = t.data_as<int64_t>();
      int64_t v = static_cast<int64_t>(value);
      for (size_t i = 0; i < n; ++i) p[i] = v;
      break;
    }
    case DType::kFloat32: {
      auto* p = t.data_as<float>();
      float v = static_cast<float>(value);
      for (size_t i = 0; i < n; ++i) p[i] = v;
      break;
    }
    case DType::kFloat64: {
      auto* p = t.data_as<double>();
      for (size_t i = 0; i < n; ++i) p[i] = value;
      break;
    }
    default:
      throw std::runtime_error("Tensor::full: unsupported dtype");
  }
  t.size_ = n;
  t.capacity_ = n;
  return t;
}

Tensor Tensor::from_data(std::vector<int32_t> data) {
  Tensor t(DType::kInt32, data.size());
  std::memcpy(t.mutable_data(), data.data(), data.size() * sizeof(int32_t));
  t.size_ = data.size();
  t.capacity_ = data.size();
  return t;
}

Tensor Tensor::from_data(std::vector<int64_t> data) {
  Tensor t(DType::kInt64, data.size());
  std::memcpy(t.mutable_data(), data.data(), data.size() * sizeof(int64_t));
  t.size_ = data.size();
  t.capacity_ = data.size();
  return t;
}

Tensor Tensor::from_data(std::vector<float> data) {
  Tensor t(DType::kFloat32, data.size());
  std::memcpy(t.mutable_data(), data.data(), data.size() * sizeof(float));
  t.size_ = data.size();
  t.capacity_ = data.size();
  return t;
}

Tensor Tensor::from_data(std::vector<double> data) {
  Tensor t(DType::kFloat64, data.size());
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
  if (dtype_ != DType::kFloat64)
    throw std::runtime_error("append_double on non-double tensor");
  ensure_capacity(size_ + 1);
  data_as<double>()[size_] = v;
  ++size_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Quantization — ggml-style Q8_0 block quantization
// ─────────────────────────────────────────────────────────────────────────────

Tensor Tensor::quantize_q8_0() const {
  if (!is_float(dtype_))
    throw std::runtime_error("quantize_q8_0: input must be a float tensor");

  const size_t n = size_;
  const size_t blocks = (n + kQ8BlockSize - 1) / kQ8BlockSize;
  Tensor result(DType::kQ8_0, n);
  auto* out = result.data_as<BlockQ8_0>();
  result.size_ = n;
  result.capacity_ = n;

  if (dtype_ == DType::kFloat32) {
    const auto* p = data_as<float>();
    for (size_t b = 0; b < blocks; ++b) {
      const size_t base = b * kQ8BlockSize;
      const size_t cnt = std::min<size_t>(kQ8BlockSize, n - base);
      float amax = 0.0f;
      for (size_t i = 0; i < cnt; ++i)
        amax = std::max(amax, std::fabs(p[base + i]));
      float d = amax / 127.0f;
      out[b].d = d == 0.0f ? 0.0f : d;
      for (size_t i = 0; i < cnt; ++i) {
        float q = d == 0.0f ? 0.0f : p[base + i] / d;
        out[b].qs[i] = static_cast<int8_t>(std::max(-127.0f,
                                                    std::min(127.0f, q)));
      }
      for (size_t i = cnt; i < kQ8BlockSize; ++i) out[b].qs[i] = 0;
    }
  } else {
    const auto* p = data_as<double>();
    for (size_t b = 0; b < blocks; ++b) {
      const size_t base = b * kQ8BlockSize;
      const size_t cnt = std::min<size_t>(kQ8BlockSize, n - base);
      double amax = 0.0;
      for (size_t i = 0; i < cnt; ++i)
        amax = std::max(amax, std::fabs(p[base + i]));
      float d = static_cast<float>(amax / 127.0);
      out[b].d = d == 0.0f ? 0.0f : d;
      for (size_t i = 0; i < cnt; ++i) {
        float q = d == 0.0f ? 0.0f : static_cast<float>(p[base + i]) / d;
        out[b].qs[i] = static_cast<int8_t>(std::max(-127.0f,
                                                    std::min(127.0f, q)));
      }
      for (size_t i = cnt; i < kQ8BlockSize; ++i) out[b].qs[i] = 0;
    }
  }
  return result;
}

Tensor Tensor::dequantize() const {
  if (dtype_ != DType::kQ8_0)
    throw std::runtime_error("dequantize: input must be a kQ8_0 tensor");

  const size_t n = size_;
  const size_t blocks = (n + kQ8BlockSize - 1) / kQ8BlockSize;
  Tensor result(DType::kFloat64, n);
  auto* pr = result.data_as<double>();
  const auto* q = data_as<BlockQ8_0>();
  for (size_t b = 0; b < blocks; ++b) {
    const size_t base = b * kQ8BlockSize;
    const size_t cnt = std::min<size_t>(kQ8BlockSize, n - base);
    const float d = q[b].d;
    for (size_t i = 0; i < cnt; ++i) {
      pr[base + i] = static_cast<double>(q[b].qs[i]) * static_cast<double>(d);
    }
  }
  result.resize(n);
  return result;
}

double Tensor::dot_q8_0(const Tensor& other) const {
  if (dtype_ != DType::kQ8_0 || other.dtype_ != DType::kQ8_0)
    throw std::runtime_error("dot_q8_0: both operands must be kQ8_0");
  if (size_ != other.size_)
    throw std::runtime_error("dot_q8_0: length mismatch");

  const size_t n = size_;
  const size_t blocks = (n + kQ8BlockSize - 1) / kQ8BlockSize;
  const auto* a = data_as<BlockQ8_0>();
  const auto* b = other.data_as<BlockQ8_0>();
  double acc = 0.0;
  for (size_t blk = 0; blk < blocks; ++blk) {
    int32_t s = 0;
    const size_t cnt = std::min<size_t>(kQ8BlockSize, n - blk * kQ8BlockSize);
    for (size_t i = 0; i < cnt; ++i) {
      s += static_cast<int32_t>(a[blk].qs[i]) *
           static_cast<int32_t>(b[blk].qs[i]);
    }
    acc += static_cast<double>(a[blk].d) * static_cast<double>(b[blk].d) *
           static_cast<double>(s);
  }
  return acc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────────────────────────────────────

Tensor Tensor::clone() const {
  Tensor t(dtype_, capacity_);
  t.size_ = size_;
  if (size_ > 0) {
    std::memcpy(t.mutable_data(), data_.data(), dtype_bytes(size_, dtype_));
  }
  return t;
}

Tensor Tensor::to_double() const {
  if (dtype_ == DType::kFloat64) return clone();
  const size_t n = size_;
  Tensor result(DType::kFloat64, n);
  auto* pr = result.data_as<double>();
  switch (dtype_) {
    case DType::kInt32: {
      const auto* p = data_as<int32_t>();
      for (size_t i = 0; i < n; ++i) pr[i] = static_cast<double>(p[i]);
      break;
    }
    case DType::kInt64: {
      const auto* p = data_as<int64_t>();
      for (size_t i = 0; i < n; ++i) pr[i] = static_cast<double>(p[i]);
      break;
    }
    case DType::kFloat32: {
      const auto* p = data_as<float>();
      for (size_t i = 0; i < n; ++i) pr[i] = static_cast<double>(p[i]);
      break;
    }
    default:
      throw std::runtime_error("Tensor::to_double: unsupported dtype");
  }
  result.resize(n);
  return result;
}

void Tensor::reserve(size_t cap) { ensure_capacity(cap); }

void Tensor::resize(size_t n) {
  ensure_capacity(n);
  size_ = n;
}

}  // namespace lark::column
