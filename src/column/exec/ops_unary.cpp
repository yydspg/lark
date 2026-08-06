// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Unary ops: neg / abs / square / sqrt / exp / log.

#include <cmath>

#include "column/exec/ops_kernels.h"

namespace lark::column::exec {

namespace {

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
      ctx.set(outputs_[0], elementwise_unary(a, a.dtype(),
                                             [](auto x) { return x < 0 ? -x : x; }));
    } else if (kind_ == "square") {
      ctx.set(outputs_[0],
              elementwise_unary(a, a.dtype(), [](auto x) { return x * x; }));
    } else if (kind_ == "sqrt") {
      ctx.set(outputs_[0], elementwise_unary(a, DType::kFloat64,
                                             [](auto x) {
                                               return std::sqrt(
                                                   static_cast<double>(x));
                                             }));
    } else if (kind_ == "exp") {
      ctx.set(outputs_[0], elementwise_unary(a, DType::kFloat64,
                                             [](auto x) {
                                               return std::exp(
                                                   static_cast<double>(x));
                                             }));
    } else if (kind_ == "log") {
      ctx.set(outputs_[0], elementwise_unary(a, DType::kFloat64,
                                             [](auto x) {
                                               return std::log(
                                                   static_cast<double>(x));
                                             }));
    } else {
      throw std::runtime_error("unary op: unknown kind '" + kind_ + "'");
    }
  }

 private:
  std::string kind_;
};

}  // namespace

std::unique_ptr<TensorOp> create_unary_op(const OpSpec& spec) {
  const std::string& t = spec.type;
  if (t == "neg" || t == "abs" || t == "square" || t == "sqrt" || t == "exp" ||
      t == "log") {
    return std::make_unique<UnaryOp>(t, spec.inputs, spec.outputs, t);
  }
  return nullptr;
}

}  // namespace lark::column::exec
