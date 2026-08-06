// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

#include "column/exec/tensor_op.h"

namespace lark::column::biz {

// ─────────────────────────────────────────────────────────────────────────────
// SubGraph: the business definition of a module — WHAT to compute.
//
// A subgraph is an ordered list of business ops (OpSpec) that reference
// business column names. Ops can be added either through the builder helpers
// below (pure-code orchestration) or parsed from the DSL (see dsl.h). The
// business layer never constructs execution nodes: at compile time the
// pipeline turns every op into a backend TensorOp bound to an execution
// ComputeNode.
// ─────────────────────────────────────────────────────────────────────────────
class SubGraph {
 public:
  SubGraph& op(exec::OpSpec spec) {
    ops_.push_back(std::move(spec));
    return *this;
  }
  SubGraph& add(exec::OpSpec spec) { return op(std::move(spec)); }

  const std::vector<exec::OpSpec>& ops() const noexcept { return ops_; }
  std::size_t size() const noexcept { return ops_.size(); }
  bool empty() const noexcept { return ops_.empty(); }
  void clear() { ops_.clear(); }

 private:
  std::vector<exec::OpSpec> ops_;
};

// ─────────────────────────────────────────────────────────────────────────────
// op:: — convenience builders for pure-code orchestration.
//
//   Module m("feature");
//   m.input("x").input("y").output("score")
//    .op(ops::mul_scalar("scaled", "x", 2.0))
//    .op(ops::add("score", "scaled", "y"));
// ─────────────────────────────────────────────────────────────────────────────
namespace ops {

inline exec::OpSpec add(const std::string& out, const std::string& a,
                        const std::string& b) {
  return exec::OpSpec{"add", {a, b}, {out}};
}
inline exec::OpSpec sub(const std::string& out, const std::string& a,
                        const std::string& b) {
  return exec::OpSpec{"sub", {a, b}, {out}};
}
inline exec::OpSpec mul(const std::string& out, const std::string& a,
                        const std::string& b) {
  return exec::OpSpec{"mul", {a, b}, {out}};
}
inline exec::OpSpec div(const std::string& out, const std::string& a,
                        const std::string& b) {
  return exec::OpSpec{"div", {a, b}, {out}};
}

inline exec::OpSpec add_scalar(const std::string& out, const std::string& a,
                               double v) {
  return exec::OpSpec{"add_scalar", {a}, {out}, {{"scalar", std::to_string(v)}}};
}
inline exec::OpSpec sub_scalar(const std::string& out, const std::string& a,
                               double v) {
  return exec::OpSpec{"sub_scalar", {a}, {out}, {{"scalar", std::to_string(v)}}};
}
inline exec::OpSpec mul_scalar(const std::string& out, const std::string& a,
                               double v) {
  return exec::OpSpec{"mul_scalar", {a}, {out}, {{"scalar", std::to_string(v)}}};
}
inline exec::OpSpec div_scalar(const std::string& out, const std::string& a,
                               double v) {
  return exec::OpSpec{"div_scalar", {a}, {out}, {{"scalar", std::to_string(v)}}};
}

inline exec::OpSpec neg(const std::string& out, const std::string& a) {
  return exec::OpSpec{"neg", {a}, {out}};
}
inline exec::OpSpec abs(const std::string& out, const std::string& a) {
  return exec::OpSpec{"abs", {a}, {out}};
}
inline exec::OpSpec square(const std::string& out, const std::string& a) {
  return exec::OpSpec{"square", {a}, {out}};
}
inline exec::OpSpec sqrt(const std::string& out, const std::string& a) {
  return exec::OpSpec{"sqrt", {a}, {out}};
}
inline exec::OpSpec exp(const std::string& out, const std::string& a) {
  return exec::OpSpec{"exp", {a}, {out}};
}
inline exec::OpSpec log(const std::string& out, const std::string& a) {
  return exec::OpSpec{"log", {a}, {out}};
}

inline exec::OpSpec sum(const std::string& out, const std::string& a) {
  return exec::OpSpec{"sum", {a}, {out}};
}
inline exec::OpSpec mean(const std::string& out, const std::string& a) {
  return exec::OpSpec{"mean", {a}, {out}};
}
inline exec::OpSpec count(const std::string& out, const std::string& a) {
  return exec::OpSpec{"count", {a}, {out}};
}
inline exec::OpSpec max(const std::string& out, const std::string& a) {
  return exec::OpSpec{"max", {a}, {out}};
}
inline exec::OpSpec min(const std::string& out, const std::string& a) {
  return exec::OpSpec{"min", {a}, {out}};
}
inline exec::OpSpec stddev(const std::string& out, const std::string& a) {
  return exec::OpSpec{"stddev", {a}, {out}};
}

inline exec::OpSpec gt(const std::string& out, const std::string& a,
                       const std::string& b) {
  return exec::OpSpec{"gt", {a, b}, {out}};
}
inline exec::OpSpec ge(const std::string& out, const std::string& a,
                       const std::string& b) {
  return exec::OpSpec{"ge", {a, b}, {out}};
}
inline exec::OpSpec lt(const std::string& out, const std::string& a,
                       const std::string& b) {
  return exec::OpSpec{"lt", {a, b}, {out}};
}
inline exec::OpSpec le(const std::string& out, const std::string& a,
                       const std::string& b) {
  return exec::OpSpec{"le", {a, b}, {out}};
}
inline exec::OpSpec eq(const std::string& out, const std::string& a,
                       const std::string& b) {
  return exec::OpSpec{"eq", {a, b}, {out}};
}
inline exec::OpSpec neq(const std::string& out, const std::string& a,
                        const std::string& b) {
  return exec::OpSpec{"neq", {a, b}, {out}};
}

inline exec::OpSpec gt_scalar(const std::string& out, const std::string& a,
                              double v) {
  return exec::OpSpec{"gt_scalar", {a}, {out}, {{"scalar", std::to_string(v)}}};
}
inline exec::OpSpec ge_scalar(const std::string& out, const std::string& a,
                              double v) {
  return exec::OpSpec{"ge_scalar", {a}, {out}, {{"scalar", std::to_string(v)}}};
}
inline exec::OpSpec lt_scalar(const std::string& out, const std::string& a,
                              double v) {
  return exec::OpSpec{"lt_scalar", {a}, {out}, {{"scalar", std::to_string(v)}}};
}
inline exec::OpSpec le_scalar(const std::string& out, const std::string& a,
                              double v) {
  return exec::OpSpec{"le_scalar", {a}, {out}, {{"scalar", std::to_string(v)}}};
}
inline exec::OpSpec eq_scalar(const std::string& out, const std::string& a,
                              double v) {
  return exec::OpSpec{"eq_scalar", {a}, {out}, {{"scalar", std::to_string(v)}}};
}
inline exec::OpSpec neq_scalar(const std::string& out, const std::string& a,
                               double v) {
  return exec::OpSpec{"neq_scalar", {a}, {out},
                      {{"scalar", std::to_string(v)}}};
}

inline exec::OpSpec select(const std::string& out, const std::string& mask,
                           const std::string& a, const std::string& b) {
  return exec::OpSpec{"select", {mask, a, b}, {out}};
}
inline exec::OpSpec filter(const std::string& out, const std::string& data,
                           const std::string& mask) {
  return exec::OpSpec{"filter", {data, mask}, {out}};
}
inline exec::OpSpec cast(const std::string& out, const std::string& a,
                         const std::string& dtype) {
  return exec::OpSpec{"cast", {a}, {out}, {{"dtype", dtype}}};
}
inline exec::OpSpec quantize(const std::string& out, const std::string& a) {
  return exec::OpSpec{"quantize", {a}, {out}};
}
inline exec::OpSpec dequantize(const std::string& out, const std::string& a) {
  return exec::OpSpec{"dequantize", {a}, {out}};
}
inline exec::OpSpec dot(const std::string& out, const std::string& a,
                        const std::string& b) {
  return exec::OpSpec{"dot", {a, b}, {out}};
}
inline exec::OpSpec constant(const std::string& out, double value) {
  return exec::OpSpec{"const", {}, {out},
                      {{"value", std::to_string(value)}, {"size", "1"}}};
}

}  // namespace ops

}  // namespace lark::column::biz
