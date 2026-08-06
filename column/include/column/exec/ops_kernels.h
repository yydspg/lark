// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Shared execution-layer kernel helpers + op base class.
//
// These are split out of the op implementations so each op family lives in its
// own translation unit (see ops_binary.cpp / ops_unary.cpp / ops_reduce.cpp /
// ops_compare.cpp / ops_special.cpp) — keeping the execution layer modular
// instead of one monolithic file. create_op() (ops.cpp) dispatches to the
// per-family factories declared below.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "column/exec/tensor_op.h"

namespace lark::column::exec {

// ─────────────────────────────────────────────────────────────────────────────
// Dtype dispatch helpers
// ─────────────────────────────────────────────────────────────────────────────

template <typename Fn>
inline void for_each_scalar_type(DType dtype, Fn&& fn) {
  switch (dtype) {
    case DType::kInt32:
      fn.template operator()<int32_t>();
      return;
    case DType::kInt64:
      fn.template operator()<int64_t>();
      return;
    case DType::kFloat32:
      fn.template operator()<float>();
      return;
    case DType::kFloat64:
      fn.template operator()<double>();
      return;
    default:
      throw std::runtime_error("op: unsupported dtype");
  }
}

// Promoted output dtype for a binary op: int32+int64 → int64; mixed int/float
// → float; float32+float64 → float64.
inline DType promote(DType a, DType b) {
  if (a == b) return a;
  const bool af = is_float(a);
  const bool bf = is_float(b);
  if (af && bf) return DType::kFloat64;
  if (af || bf) {
    const DType f = af ? a : b;
    const DType i = af ? b : a;
    return i == DType::kInt64 ? DType::kFloat64 : f;
  }
  return DType::kInt64;
}

// Promote an int scalar context: for an int tensor the scalar is truncated.
inline DType promote_scalar(DType a) {
  return a == DType::kInt64   ? DType::kInt64
         : a == DType::kInt32 ? DType::kInt32
                              : a;
}

inline Tensor cast_tensor(const Tensor& a, DType to) {
  if (a.dtype() == to) return a.clone();
  if (a.dtype() == DType::kQ8_0) return a.dequantize();
  if (to == DType::kQ8_0) return a.quantize_q8_0();

  const size_t n = a.size();
  Tensor r(to, n);
  const auto copy = [&]<typename T>() {
    const T* p = a.data_as<T>();
    switch (to) {
      case DType::kInt32: {
        auto* pr = r.data_as<int32_t>();
        for (size_t i = 0; i < n; ++i) pr[i] = static_cast<int32_t>(p[i]);
        break;
      }
      case DType::kInt64: {
        auto* pr = r.data_as<int64_t>();
        for (size_t i = 0; i < n; ++i) pr[i] = static_cast<int64_t>(p[i]);
        break;
      }
      case DType::kFloat32: {
        auto* pr = r.data_as<float>();
        for (size_t i = 0; i < n; ++i) pr[i] = static_cast<float>(p[i]);
        break;
      }
      case DType::kFloat64: {
        auto* pr = r.data_as<double>();
        for (size_t i = 0; i < n; ++i) pr[i] = static_cast<double>(p[i]);
        break;
      }
      default:
        throw std::runtime_error("cast_tensor: unsupported target dtype");
    }
  };
  switch (a.dtype()) {
    case DType::kInt32:
      copy.template operator()<int32_t>();
      break;
    case DType::kInt64:
      copy.template operator()<int64_t>();
      break;
    case DType::kFloat32:
      copy.template operator()<float>();
      break;
    case DType::kFloat64:
      copy.template operator()<double>();
      break;
    default:
      throw std::runtime_error("cast_tensor: unsupported source dtype");
  }
  r.resize(n);
  return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic elementwise kernels
// ─────────────────────────────────────────────────────────────────────────────

template <typename Fn>
inline Tensor elementwise_binary(const Tensor& a, const Tensor& b,
                                 DType out_dtype, Fn&& fn) {
  const size_t n = a.size();
  if (b.size() != n) {
    throw std::runtime_error("binary op: length mismatch (" +
                             std::to_string(n) + " vs " +
                             std::to_string(b.size()) + ")");
  }
  Tensor result(out_dtype, n);
  Tensor a_c, b_c;
  const Tensor& ra =
      a.dtype() == out_dtype ? a : (a_c = cast_tensor(a, out_dtype), a_c);
  const Tensor& rb =
      b.dtype() == out_dtype ? b : (b_c = cast_tensor(b, out_dtype), b_c);
  for_each_scalar_type(out_dtype, [&]<typename T>() {
    const auto* pa = ra.data_as<T>();
    const auto* pb = rb.data_as<T>();
    auto* pr = result.data_as<T>();
    for (size_t i = 0; i < n; ++i) pr[i] = fn(pa[i], pb[i]);
  });
  result.resize(n);
  return result;
}

template <typename Fn>
inline Tensor elementwise_scalar(const Tensor& a, double v, DType out_dtype,
                                 Fn&& fn) {
  const size_t n = a.size();
  Tensor result(out_dtype, n);
  Tensor a_c;
  const Tensor& ra =
      a.dtype() == out_dtype ? a : (a_c = cast_tensor(a, out_dtype), a_c);
  for_each_scalar_type(out_dtype, [&]<typename T>() {
    const auto* pa = ra.data_as<T>();
    auto* pr = result.data_as<T>();
    const T s = static_cast<T>(v);
    for (size_t i = 0; i < n; ++i) pr[i] = fn(pa[i], s);
  });
  result.resize(n);
  return result;
}

template <typename Fn>
inline Tensor elementwise_unary(const Tensor& a, DType out_dtype, Fn&& fn) {
  const size_t n = a.size();
  Tensor result(out_dtype, n);
  Tensor a_c;
  const Tensor& ra =
      a.dtype() == out_dtype ? a : (a_c = cast_tensor(a, out_dtype), a_c);
  for_each_scalar_type(out_dtype, [&]<typename T>() {
    const auto* pa = ra.data_as<T>();
    auto* pr = result.data_as<T>();
    for (size_t i = 0; i < n; ++i) pr[i] = fn(pa[i]);
  });
  result.resize(n);
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Common op base
// ─────────────────────────────────────────────────────────────────────────────
class OpBase : public TensorOp {
 public:
  OpBase(std::string type, std::vector<std::string> inputs,
         std::vector<std::string> outputs)
      : type_(std::move(type)),
        inputs_(std::move(inputs)),
        outputs_(std::move(outputs)) {}

  const char* op_type() const noexcept override { return type_.c_str(); }
  const std::vector<std::string>& inputs() const noexcept override {
    return inputs_;
  }
  const std::vector<std::string>& outputs() const noexcept override {
    return outputs_;
  }

 protected:
  std::string type_;
  std::vector<std::string> inputs_;
  std::vector<std::string> outputs_;
};

inline double ParseScalar(const std::string& s) { return std::stod(s); }

// ─────────────────────────────────────────────────────────────────────────────
// Per-family factories (implemented in ops_*.cpp; called by create_op).
// Each returns nullptr when the spec does not belong to its family.
// ─────────────────────────────────────────────────────────────────────────────
std::unique_ptr<TensorOp> create_binary_op(const OpSpec& spec);
std::unique_ptr<TensorOp> create_unary_op(const OpSpec& spec);
std::unique_ptr<TensorOp> create_reduce_op(const OpSpec& spec);
std::unique_ptr<TensorOp> create_compare_op(const OpSpec& spec);
std::unique_ptr<TensorOp> create_special_op(const OpSpec& spec);

}  // namespace lark::column::exec
