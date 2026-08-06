// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Reduction ops: sum / mean / count / min / max / stddev.

#include <cmath>

#include "column/exec/ops_kernels.h"

namespace lark::column::exec {

namespace {

double tensor_sum(const Tensor& a) {
  switch (a.dtype()) {
    case DType::kInt32: {
      const auto* p = a.data_as<int32_t>();
      int64_t s = 0;
      for (size_t i = 0; i < a.size(); ++i) s += p[i];
      return static_cast<double>(s);
    }
    case DType::kInt64: {
      const auto* p = a.data_as<int64_t>();
      int64_t s = 0;
      for (size_t i = 0; i < a.size(); ++i) s += p[i];
      return static_cast<double>(s);
    }
    case DType::kFloat32: {
      const auto* p = a.data_as<float>();
      double s = 0.0;
      for (size_t i = 0; i < a.size(); ++i) s += p[i];
      return s;
    }
    case DType::kFloat64: {
      const auto* p = a.data_as<double>();
      double s = 0.0;
      for (size_t i = 0; i < a.size(); ++i) s += p[i];
      return s;
    }
    default:
      throw std::runtime_error("reduce: unsupported dtype");
  }
}

Tensor reduce_to_scalar(DType out_dtype, double value) {
  Tensor r(out_dtype, 1);
  for_each_scalar_type(out_dtype, [&]<typename T>() {
    r.data_as<T>()[0] = static_cast<T>(value);
  });
  r.resize(1);
  return r;
}

Tensor reduce_minmax(const Tensor& a, bool want_max) {
  Tensor r(a.dtype(), 1);
  const size_t n = a.size();
  if (n == 0) {
    for_each_scalar_type(a.dtype(), [&]<typename T>() {
      r.data_as<T>()[0] = static_cast<T>(0);
    });
    r.resize(1);
    return r;
  }
  for_each_scalar_type(a.dtype(), [&]<typename T>() {
    const auto* p = a.data_as<T>();
    T acc = p[0];
    for (size_t i = 1; i < n; ++i) {
      if (want_max ? p[i] > acc : p[i] < acc) acc = p[i];
    }
    r.data_as<T>()[0] = acc;
  });
  r.resize(1);
  return r;
}

class ReduceOp : public OpBase {
 public:
  ReduceOp(std::string type, std::vector<std::string> inputs,
           std::vector<std::string> outputs, std::string kind)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        kind_(std::move(kind)) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    if (kind_ == "sum") {
      ctx.set(outputs_[0],
              reduce_to_scalar(DType::kFloat64, tensor_sum(a)));
    } else if (kind_ == "mean") {
      const double m = a.empty() ? 0.0
                                 : tensor_sum(a) / static_cast<double>(a.size());
      ctx.set(outputs_[0], reduce_to_scalar(DType::kFloat64, m));
    } else if (kind_ == "count") {
      ctx.set(outputs_[0], reduce_to_scalar(
                               DType::kInt64, static_cast<double>(a.size())));
    } else if (kind_ == "max") {
      ctx.set(outputs_[0], reduce_minmax(a, true));
    } else if (kind_ == "min") {
      ctx.set(outputs_[0], reduce_minmax(a, false));
    } else if (kind_ == "stddev") {
      const size_t n = a.size();
      const double mean =
          n == 0 ? 0.0 : tensor_sum(a) / static_cast<double>(n);
      double sq = 0.0;
      for (size_t i = 0; i < n; ++i) {
        double x = 0.0;
        if (a.dtype() == DType::kInt32) {
          x = static_cast<double>(a.data_as<int32_t>()[i]);
        } else if (a.dtype() == DType::kInt64) {
          x = static_cast<double>(a.data_as<int64_t>()[i]);
        } else if (a.dtype() == DType::kFloat32) {
          x = static_cast<double>(a.data_as<float>()[i]);
        } else {
          x = a.data_as<double>()[i];
        }
        const double d = x - mean;
        sq += d * d;
      }
      const double sd = n == 0 ? 0.0 : std::sqrt(sq / static_cast<double>(n));
      ctx.set(outputs_[0], reduce_to_scalar(DType::kFloat64, sd));
    } else {
      throw std::runtime_error("reduce op: unknown kind '" + kind_ + "'");
    }
  }

 private:
  std::string kind_;
};

}  // namespace

std::unique_ptr<TensorOp> create_reduce_op(const OpSpec& spec) {
  const std::string& t = spec.type;
  if (t == "sum" || t == "mean" || t == "count" || t == "max" || t == "min" ||
      t == "stddev") {
    return std::make_unique<ReduceOp>(t, spec.inputs, spec.outputs, t);
  }
  return nullptr;
}

}  // namespace lark::column::exec
