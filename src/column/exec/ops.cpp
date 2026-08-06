// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Execution-layer built-in tensor ops (CPU backend).
//
// Each op reads its inputs and writes its outputs through the execution
// store. Kernels dispatch over the four scalar dtypes (int32/int64/float32/
// float64) plus the Q8_0 block-quantized type, and the hot loops are marked
// for vectorization.

#include "column/exec/tensor_op.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace lark::column::exec {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Dtype dispatch helpers
// ─────────────────────────────────────────────────────────────────────────────

template <typename Fn>
void for_each_scalar_type(DType dtype, Fn&& fn) {
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
DType promote(DType a, DType b) {
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
DType promote_scalar(DType a) {
  return a == DType::kInt64 ? DType::kInt64
         : a == DType::kInt32 ? DType::kInt32
         : a;
}

Tensor cast_tensor(const Tensor& a, DType to) {
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
Tensor elementwise_binary(const Tensor& a, const Tensor& b, DType out_dtype,
                          Fn&& fn) {
  const size_t n = a.size();
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
Tensor elementwise_scalar(const Tensor& a, double v, DType out_dtype, Fn&& fn) {
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
Tensor elementwise_unary(const Tensor& a, DType out_dtype, Fn&& fn) {
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
// Reductions
// ─────────────────────────────────────────────────────────────────────────────

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

double ParseScalar(const std::string& s) {
  return std::stod(s);
}

class BinaryArithOp : public OpBase {
 public:
  BinaryArithOp(std::string type, std::vector<std::string> inputs,
                std::vector<std::string> outputs, char op)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        op_(op) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    const Tensor& b = ctx.input(inputs_[1]);
    const DType out = op_ == '/' ? DType::kFloat64 : promote(a.dtype(), b.dtype());
    Tensor r;
    switch (op_) {
      case '+':
        r = elementwise_binary(a, b, out,
                               [](auto x, auto y) { return x + y; });
        break;
      case '-':
        r = elementwise_binary(a, b, out,
                               [](auto x, auto y) { return x - y; });
        break;
      case '*':
        r = elementwise_binary(a, b, out,
                               [](auto x, auto y) { return x * y; });
        break;
      case '/':
        r = elementwise_binary(a, b, DType::kFloat64,
                               [](auto x, auto y) {
                                 return static_cast<double>(x) /
                                        static_cast<double>(y);
                               });
        break;
    }
    ctx.set(outputs_[0], std::move(r));
  }

 private:
  char op_;
};

class ScalarArithOp : public OpBase {
 public:
  ScalarArithOp(std::string type, std::vector<std::string> inputs,
                std::vector<std::string> outputs, char op,
                std::unordered_map<std::string, std::string> attrs)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        op_(op),
        scalar_(ParseScalar(attrs.at("scalar"))) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    const DType out = op_ == '/' ? DType::kFloat64
                                 : promote_scalar(a.dtype());
    Tensor r;
    switch (op_) {
      case '+':
        r = elementwise_scalar(a, scalar_, out,
                               [](auto x, auto s) { return x + s; });
        break;
      case '-':
        r = elementwise_scalar(a, scalar_, out,
                               [](auto x, auto s) { return x - s; });
        break;
      case '*':
        r = elementwise_scalar(a, scalar_, out,
                               [](auto x, auto s) { return x * s; });
        break;
      case '/':
        r = elementwise_scalar(a, scalar_, DType::kFloat64,
                               [](auto x, auto s) {
                                 return static_cast<double>(x) / s;
                               });
        break;
    }
    ctx.set(outputs_[0], std::move(r));
  }

 private:
  char op_;
  double scalar_;
};

class UnaryOp : public OpBase {
 public:
  UnaryOp(std::string type, std::vector<std::string> inputs,
          std::vector<std::string> outputs, std::string kind)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        kind_(std::move(kind)) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    if (kind_ == "neg") {
      ctx.set(outputs_[0],
              elementwise_unary(a, a.dtype(), [](auto x) { return -x; }));
    } else if (kind_ == "abs") {
      ctx.set(outputs_[0],
              elementwise_unary(a, a.dtype(), [](auto x) {
                return x < 0 ? -x : x;
              }));
    } else if (kind_ == "square") {
      ctx.set(outputs_[0],
              elementwise_unary(a, a.dtype(), [](auto x) { return x * x; }));
    } else if (kind_ == "sqrt") {
      ctx.set(outputs_[0],
              elementwise_unary(a, DType::kFloat64, [](auto x) {
                return std::sqrt(static_cast<double>(x));
              }));
    } else if (kind_ == "exp") {
      ctx.set(outputs_[0],
              elementwise_unary(a, DType::kFloat64, [](auto x) {
                return std::exp(static_cast<double>(x));
              }));
    } else if (kind_ == "log") {
      ctx.set(outputs_[0],
              elementwise_unary(a, DType::kFloat64, [](auto x) {
                return std::log(static_cast<double>(x));
              }));
    } else {
      throw std::runtime_error("unary op: unknown kind '" + kind_ + "'");
    }
  }

 private:
  std::string kind_;
};

class ReduceOp : public OpBase {
 public:
  ReduceOp(std::string type, std::vector<std::string> inputs,
           std::vector<std::string> outputs, std::string kind)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        kind_(std::move(kind)) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    if (kind_ == "sum") {
      ctx.set(outputs_[0], reduce_to_scalar(DType::kFloat64, tensor_sum(a)));
    } else if (kind_ == "mean") {
      const double m =
          a.empty() ? 0.0 : tensor_sum(a) / static_cast<double>(a.size());
      ctx.set(outputs_[0], reduce_to_scalar(DType::kFloat64, m));
    } else if (kind_ == "count") {
      ctx.set(outputs_[0],
              reduce_to_scalar(DType::kInt64, static_cast<double>(a.size())));
    } else if (kind_ == "max") {
      ctx.set(outputs_[0], reduce_minmax(a, true));
    } else if (kind_ == "min") {
      ctx.set(outputs_[0], reduce_minmax(a, false));
    } else if (kind_ == "stddev") {
      const size_t n = a.size();
      const double mean = n == 0 ? 0.0 : tensor_sum(a) / static_cast<double>(n);
      double sq = 0.0;
      const double* v = nullptr;
      if (a.dtype() == DType::kFloat64) {
        v = a.data_as<double>();
      }
      for (size_t i = 0; i < n; ++i) {
        double x = 0.0;
        if (a.dtype() == DType::kInt32) {
          x = static_cast<double>(a.data_as<int32_t>()[i]);
        } else if (a.dtype() == DType::kInt64) {
          x = static_cast<double>(a.data_as<int64_t>()[i]);
        } else if (a.dtype() == DType::kFloat32) {
          x = static_cast<double>(a.data_as<float>()[i]);
        } else {
          x = v[i];
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

class CompareOp : public OpBase {
 public:
  CompareOp(std::string type, std::vector<std::string> inputs,
            std::vector<std::string> outputs, std::string kind)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        kind_(std::move(kind)) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    const Tensor& b = ctx.input(inputs_[1]);
    const size_t n = a.size();
    Tensor r(DType::kInt64, n);
    auto* pr = r.data_as<int64_t>();
    const DType d = promote(a.dtype(), b.dtype());
    for_each_scalar_type(d, [&]<typename T>() {
      Tensor a_c, b_c;
      const Tensor& ra =
          a.dtype() == d ? a : (a_c = cast_tensor(a, d), a_c);
      const Tensor& rb =
          b.dtype() == d ? b : (b_c = cast_tensor(b, d), b_c);
      const auto* pa = ra.data_as<T>();
      const auto* pb = rb.data_as<T>();
      const auto cmp = [&](auto x, auto y) -> int64_t {
        if (kind_ == "gt") return x > y;
        if (kind_ == "ge") return x >= y;
        if (kind_ == "lt") return x < y;
        if (kind_ == "le") return x <= y;
        if (kind_ == "eq") return x == y;
        return x != y;  // neq
      };
      for (size_t i = 0; i < n; ++i) pr[i] = cmp(pa[i], pb[i]);
    });
    r.resize(n);
    ctx.set(outputs_[0], std::move(r));
  }

 private:
  std::string kind_;
};

class CompareScalarOp : public OpBase {
 public:
  CompareScalarOp(std::string type, std::vector<std::string> inputs,
                  std::vector<std::string> outputs, std::string kind,
                  std::unordered_map<std::string, std::string> attrs)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        kind_(std::move(kind)),
        scalar_(ParseScalar(attrs.at("scalar"))) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    const size_t n = a.size();
    Tensor r(DType::kInt64, n);
    auto* pr = r.data_as<int64_t>();
    for_each_scalar_type(a.dtype(), [&]<typename T>() {
      const auto* pa = a.data_as<T>();
      const T s = static_cast<T>(scalar_);
      const auto cmp = [&](auto x) -> int64_t {
        if (kind_ == "gt_scalar") return x > s;
        if (kind_ == "ge_scalar") return x >= s;
        if (kind_ == "lt_scalar") return x < s;
        if (kind_ == "le_scalar") return x <= s;
        if (kind_ == "eq_scalar") return x == s;
        return x != s;  // neq_scalar
      };
      for (size_t i = 0; i < n; ++i) pr[i] = cmp(pa[i]);
    });
    r.resize(n);
    ctx.set(outputs_[0], std::move(r));
  }

 private:
  std::string kind_;
  double scalar_;
};

class SelectOp : public OpBase {
 public:
  SelectOp(std::string type, std::vector<std::string> inputs,
           std::vector<std::string> outputs)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)) {}

  void compute(OpContext& ctx) override {
    const Tensor& mask = ctx.input(inputs_[0]);
    const Tensor& a = ctx.input(inputs_[1]);
    const Tensor& b = ctx.input(inputs_[2]);
    if (mask.dtype() != DType::kInt64)
      throw std::runtime_error("select: mask must be int64");
    const size_t n = a.size();
    if (mask.size() != n || b.size() != n)
      throw std::runtime_error("select: length mismatch");
    const DType out = promote(a.dtype(), b.dtype());
    Tensor r(out, n);
    const auto* m = mask.data_as<int64_t>();
    for_each_scalar_type(out, [&]<typename T>() {
      Tensor a_c, b_c;
      const Tensor& ra = a.dtype() == out ? a : (a_c = cast_tensor(a, out), a_c);
      const Tensor& rb = b.dtype() == out ? b : (b_c = cast_tensor(b, out), b_c);
      const auto* pa = ra.data_as<T>();
      const auto* pb = rb.data_as<T>();
      auto* pr = r.data_as<T>();
      for (size_t i = 0; i < n; ++i) pr[i] = m[i] ? pa[i] : pb[i];
    });
    r.resize(n);
    ctx.set(outputs_[0], std::move(r));
  }
};

class FilterOp : public OpBase {
 public:
  FilterOp(std::string type, std::vector<std::string> inputs,
           std::vector<std::string> outputs)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)) {}

  void compute(OpContext& ctx) override {
    const Tensor& data = ctx.input(inputs_[0]);
    const Tensor& mask = ctx.input(inputs_[1]);
    if (mask.dtype() != DType::kInt64)
      throw std::runtime_error("filter: mask must be int64");
    if (mask.size() != data.size())
      throw std::runtime_error("filter: mask length mismatch");
    if (is_quantized(data.dtype()))
      throw std::runtime_error("filter: cannot filter a quantized tensor");
    const size_t n = data.size();
    const auto* m = mask.data_as<int64_t>();
    size_t keep = 0;
    for (size_t i = 0; i < n; ++i)
      if (m[i]) ++keep;
    const size_t esz = dtype_size(data.dtype());
    Tensor out(data.dtype(), keep);
    const uint8_t* src = data.data();
    uint8_t* dst = out.mutable_data();
    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
      if (m[i]) {
        std::memcpy(dst + j * esz, src + i * esz, esz);
        ++j;
      }
    }
    out.resize(keep);
    ctx.set(outputs_[0], std::move(out));
  }
};

class CastOp : public OpBase {
 public:
  CastOp(std::string type, std::vector<std::string> inputs,
         std::vector<std::string> outputs, std::string dtype_name)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        dtype_(parse_dtype(dtype_name)) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    ctx.set(outputs_[0], cast_tensor(a, dtype_));
  }

 private:
  DType dtype_;
};

class QuantizeOp : public OpBase {
 public:
  QuantizeOp(std::string type, std::vector<std::string> inputs,
             std::vector<std::string> outputs)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    if (!is_float(a.dtype()))
      throw std::runtime_error("quantize: input must be a float tensor");
    ctx.set(outputs_[0], a.quantize_q8_0());
  }
};

class DequantizeOp : public OpBase {
 public:
  DequantizeOp(std::string type, std::vector<std::string> inputs,
               std::vector<std::string> outputs)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    if (!is_quantized(a.dtype()))
      throw std::runtime_error("dequantize: input must be a q8_0 tensor");
    ctx.set(outputs_[0], a.dequantize());
  }
};

class DotQ8Op : public OpBase {
 public:
  DotQ8Op(std::string type, std::vector<std::string> inputs,
          std::vector<std::string> outputs)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    const Tensor& b = ctx.input(inputs_[1]);
    if (!is_quantized(a.dtype()) || !is_quantized(b.dtype()))
      throw std::runtime_error("dot: both inputs must be q8_0 tensors");
    const double v = a.dot_q8_0(b);
    Tensor r(DType::kFloat64, 1);
    r.data_as<double>()[0] = v;
    r.resize(1);
    ctx.set(outputs_[0], std::move(r));
  }
};

class ConstOp : public OpBase {
 public:
  ConstOp(std::string type, std::vector<std::string> inputs,
          std::vector<std::string> outputs,
          std::unordered_map<std::string, std::string> attrs)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        attrs_(std::move(attrs)) {}

  void compute(OpContext& ctx) override {
    const std::string& value_str = attrs_.at("value");
    const DType dtype = parse_dtype(attrs_.count("dtype") ? attrs_.at("dtype")
                                                          : "float64");
    const std::string& size_str =
        attrs_.count("size") ? attrs_.at("size") : "1";
    const size_t size = std::stoull(size_str);

    Tensor t = BuildConst(value_str, dtype, size);
    ctx.set(outputs_[0], std::move(t));
  }

  static Tensor BuildConst(const std::string& value_str, DType dtype,
                           size_t size) {
    const bool is_list = value_str.find(',') != std::string::npos;
    if (!is_list) {
      const double v = std::stod(value_str);
      return Tensor::full(size, v, dtype);
    }
    std::vector<double> values;
    size_t pos = 0;
    while (pos <= value_str.size()) {
      size_t comma = value_str.find(',', pos);
      if (comma == std::string::npos) comma = value_str.size();
      values.push_back(std::stod(value_str.substr(pos, comma - pos)));
      pos = comma + 1;
      if (comma == value_str.size()) break;
    }
    Tensor t(dtype, values.size());
    for_each_scalar_type(dtype, [&]<typename T>() {
      auto* p = t.data_as<T>();
      for (size_t i = 0; i < values.size(); ++i)
        p[i] = static_cast<T>(values[i]);
    });
    t.resize(values.size());
    return t;
  }

 private:
  std::unordered_map<std::string, std::string> attrs_;
};

class IdentityOp : public OpBase {
 public:
  IdentityOp(std::string type, std::vector<std::string> inputs,
             std::vector<std::string> outputs)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)) {}

  void compute(OpContext& ctx) override {
    if (inputs_[0] == outputs_[0]) return;  // pure ordering anchor (no-op)
    ctx.set(outputs_[0], ctx.input(inputs_[0]).clone());
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Factory
// ─────────────────────────────────────────────────────────────────────────────

}  // namespace

std::unique_ptr<TensorOp> create_op(const OpSpec& spec) {
  const std::string& t = spec.type;

  if (t == "add" || t == "sub" || t == "mul" || t == "div") {
    const char op = t == "add" ? '+' : t == "sub" ? '-' : t == "mul" ? '*' : '/';
    return std::make_unique<BinaryArithOp>(t, spec.inputs, spec.outputs, op);
  }
  if (t == "add_scalar" || t == "sub_scalar" || t == "mul_scalar" ||
      t == "div_scalar") {
    const char op = t == "add_scalar"
                        ? '+'
                        : t == "sub_scalar" ? '-' : t == "mul_scalar" ? '*' : '/';
    if (!spec.has_attr("scalar"))
      throw std::invalid_argument("scalar op '" + t + "' requires attr 'scalar'");
    return std::make_unique<ScalarArithOp>(t, spec.inputs, spec.outputs, op,
                                           spec.attrs);
  }
  if (t == "neg" || t == "abs" || t == "square" || t == "sqrt" || t == "exp" ||
      t == "log") {
    return std::make_unique<UnaryOp>(t, spec.inputs, spec.outputs, t);
  }
  if (t == "sum" || t == "mean" || t == "count" || t == "max" || t == "min" ||
      t == "stddev") {
    return std::make_unique<ReduceOp>(t, spec.inputs, spec.outputs, t);
  }
  if (t == "gt" || t == "ge" || t == "lt" || t == "le" || t == "eq" ||
      t == "neq") {
    return std::make_unique<CompareOp>(t, spec.inputs, spec.outputs, t);
  }
  if (t == "gt_scalar" || t == "ge_scalar" || t == "lt_scalar" ||
      t == "le_scalar" || t == "eq_scalar" || t == "neq_scalar") {
    if (!spec.has_attr("scalar"))
      throw std::invalid_argument(
          "scalar compare '" + t + "' requires attr 'scalar'");
    return std::make_unique<CompareScalarOp>(t, spec.inputs, spec.outputs, t,
                                             spec.attrs);
  }
  if (t == "select") {
    if (spec.inputs.size() != 3)
      throw std::invalid_argument("select requires 3 inputs (mask,a,b)");
    return std::make_unique<SelectOp>(t, spec.inputs, spec.outputs);
  }
  if (t == "filter") {
    if (spec.inputs.size() != 2)
      throw std::invalid_argument("filter requires 2 inputs (data,mask)");
    return std::make_unique<FilterOp>(t, spec.inputs, spec.outputs);
  }
  if (t == "cast") {
    if (!spec.has_attr("dtype"))
      throw std::invalid_argument("cast requires attr 'dtype'");
    return std::make_unique<CastOp>(t, spec.inputs, spec.outputs,
                                    spec.attrs.at("dtype"));
  }
  if (t == "quantize") {
    return std::make_unique<QuantizeOp>(t, spec.inputs, spec.outputs);
  }
  if (t == "dequantize") {
    return std::make_unique<DequantizeOp>(t, spec.inputs, spec.outputs);
  }
  if (t == "dot") {
    return std::make_unique<DotQ8Op>(t, spec.inputs, spec.outputs);
  }
  if (t == "const") {
    if (!spec.has_attr("value"))
      throw std::invalid_argument("const requires attr 'value'");
    return std::make_unique<ConstOp>(t, spec.inputs, spec.outputs, spec.attrs);
  }
  if (t == "identity") {
    return std::make_unique<IdentityOp>(t, spec.inputs, spec.outputs);
  }

  throw std::invalid_argument("create_op: unknown op type '" + t + "'");
}

}  // namespace lark::column::exec
