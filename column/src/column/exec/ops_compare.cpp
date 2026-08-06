// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Comparison ops: gt / ge / lt / le / eq / neq (+ *_scalar variants).
// Outputs are int64 masks (1 = true, 0 = false).

#include "column/exec/ops_kernels.h"

namespace lark::column::exec {

namespace {

class CompareOp : public OpBase {
 public:
  CompareOp(std::string type, std::vector<std::string> inputs,
            std::vector<std::string> outputs, std::string kind)
      : OpBase(std::move(type), std::move(inputs), std::move(outputs)),
        kind_(std::move(kind)) {}

  void compute(OpContext& ctx) override {
    const Tensor& a = ctx.input(inputs_[0]);
    const Tensor& b = ctx.input(inputs_[1]);
    if (a.size() != b.size()) {
      throw std::runtime_error("compare: length mismatch (" +
                               std::to_string(a.size()) + " vs " +
                               std::to_string(b.size()) + ")");
    }
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

}  // namespace

std::unique_ptr<TensorOp> create_compare_op(const OpSpec& spec) {
  const std::string& t = spec.type;
  if (t == "gt" || t == "ge" || t == "lt" || t == "le" || t == "eq" ||
      t == "neq") {
    return std::make_unique<CompareOp>(t, spec.inputs, spec.outputs, t);
  }
  if (t == "gt_scalar" || t == "ge_scalar" || t == "lt_scalar" ||
      t == "le_scalar" || t == "eq_scalar" || t == "neq_scalar") {
    if (!spec.has_attr("scalar"))
      throw std::invalid_argument("scalar compare '" + t +
                                  "' requires attr 'scalar'");
    return std::make_unique<CompareScalarOp>(t, spec.inputs, spec.outputs, t,
                                             spec.attrs);
  }
  return nullptr;
}

}  // namespace lark::column::exec
