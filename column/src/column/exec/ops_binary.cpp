// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Binary arithmetic ops: add / sub / mul / div (+ *_scalar variants).

#include "column/exec/ops_kernels.h"

namespace lark::column::exec {

namespace {

class BinaryArithOp : public OpBase {
 public:
  BinaryArithOp(std::string type, std::vector<std::string> inputs,
                std::vector<std::string> outputs, char op)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        op_(op) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    const Tensor& b = ctx.input(inputs_[1]);
    Tensor r;
    if (op_ == '/') {
      r = elementwise_binary(a, b, DType::kFloat64,
                             [](auto x, auto y) {
                               return static_cast<double>(x) /
                                      static_cast<double>(y);
                             });
    } else {
      const DType out = promote(a.dtype(), b.dtype());
      switch (op_) {
        case '+':
          r = elementwise_binary(a, b, out,
                                 [](auto x, auto y) { return x + y; });
          break;
        case '-':
          r = elementwise_binary(a, b, out,
                                 [](auto x, auto y) { return x - y; });
          break;
        default:
          r = elementwise_binary(a, b, out,
                                 [](auto x, auto y) { return x * y; });
          break;
      }
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
    const DType out =
        op_ == '/' ? DType::kFloat64 : promote_scalar(a.dtype());
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
      default:
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

}  // namespace

std::unique_ptr<TensorOp> create_binary_op(const OpSpec& spec) {
  const std::string& t = spec.type;
  if (t == "add" || t == "sub" || t == "mul" || t == "div") {
    const char op =
        t == "add" ? '+' : t == "sub" ? '-' : t == "mul" ? '*' : '/';
    return std::make_unique<BinaryArithOp>(t, spec.inputs, spec.outputs, op);
  }
  if (t == "add_scalar" || t == "sub_scalar" || t == "mul_scalar" ||
      t == "div_scalar") {
    const char op = t == "add_scalar"
                        ? '+'
                        : t == "sub_scalar" ? '-' : t == "mul_scalar" ? '*' : '/';
    if (!spec.has_attr("scalar"))
      throw std::invalid_argument("scalar op '" + t +
                                  "' requires attr 'scalar'");
    return std::make_unique<ScalarArithOp>(t, spec.inputs, spec.outputs, op,
                                           spec.attrs);
  }
  return nullptr;
}

}  // namespace lark::column::exec
