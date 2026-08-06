// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Special ops: select / filter / cast / quantize / dequantize / dot / const /
// identity.

#include <cstring>

#include "column/exec/ops_kernels.h"

namespace lark::column::exec {

namespace {

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
      const Tensor& ra =
          a.dtype() == out ? a : (a_c = cast_tensor(a, out), a_c);
      const Tensor& rb =
          b.dtype() == out ? b : (b_c = cast_tensor(b, out), b_c);
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
    ctx.set(outputs_[0], BuildConst(value_str, dtype, size));
  }

  static Tensor BuildConst(const std::string& value_str, DType dtype,
                           size_t size) {
    const bool is_list = value_str.find(',') != std::string::npos;
    if (!is_list) {
      return Tensor::full(size, std::stod(value_str), dtype);
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

}  // namespace

std::unique_ptr<TensorOp> create_special_op(const OpSpec& spec) {
  const std::string& t = spec.type;
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
  return nullptr;
}

}  // namespace lark::column::exec
