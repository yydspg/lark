// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "column/tensor_store.h"

namespace lark::column::exec {

// ─────────────────────────────────────────────────────────────────────────────
// OpSpec: the serializable description of a computation op.
//
// This is the unit that flows between the business layer (DSL / module code)
// and the execution layer (backend factory → TensorOp).
//
//   spec.type     — "add", "mul_scalar", "sum", "filter", "quantize", ...
//   spec.inputs   — tensor names this op reads from the store
//   spec.outputs  — tensor names this op writes into the store
//   spec.attrs    — string-valued attributes ("scalar", "dtype", ...)
// ─────────────────────────────────────────────────────────────────────────────
struct OpSpec {
  std::string type;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::unordered_map<std::string, std::string> attrs;

  OpSpec() = default;
  OpSpec(std::string type, std::vector<std::string> inputs,
         std::vector<std::string> outputs,
         std::unordered_map<std::string, std::string> attrs = {})
      : type(std::move(type)),
        inputs(std::move(inputs)),
        outputs(std::move(outputs)),
        attrs(std::move(attrs)) {}

  // Convenience accessors for common attributes.
  bool has_attr(const std::string& key) const {
    return attrs.find(key) != attrs.end();
  }
  const std::string& attr(const std::string& key,
                          const std::string& fallback = {}) const {
    auto it = attrs.find(key);
    return it == attrs.end() ? fallback : it->second;
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// OpContext: the read/write view handed to a TensorOp during compute().
//
// Ops read their inputs and write their outputs through the execution store;
// OpContext is a thin wrapper so op implementations stay small and testable.
// ─────────────────────────────────────────────────────────────────────────────
class OpContext {
 public:
  explicit OpContext(TensorStore& store) : store_(&store) {}

  const Tensor& input(const std::string& name) const;
  const Tensor& operator[](const std::string& name) const { return input(name); }
  bool has(const std::string& name) const { return store_->has(name); }

  // Write an op output into the store.
  void set(const std::string& name, Tensor tensor) {
    store_->set(name, std::move(tensor));
  }

  TensorStore& store() noexcept { return *store_; }
  const TensorStore& store() const noexcept { return *store_; }

 private:
  TensorStore* store_;
};

// ─────────────────────────────────────────────────────────────────────────────
// TensorOp: an execution-layer computation bound to a compute node.
//
// Business-layer nodes are ergonomic wrappers; execution-layer ops carry the
// actual (performance-sensitive) kernel. A ComputeNode binds exactly one
// TensorOp.
// ─────────────────────────────────────────────────────────────────────────────
class TensorOp {
 public:
  virtual ~TensorOp() = default;

  virtual const char* op_type() const noexcept = 0;
  virtual const std::vector<std::string>& inputs() const noexcept = 0;
  virtual const std::vector<std::string>& outputs() const noexcept = 0;

  // Execute the kernel against the store via ctx.
  virtual void compute(OpContext& ctx) = 0;
};

// Built-in op factory (CPU backend). Throws std::invalid_argument on an
// unknown op type.
std::unique_ptr<TensorOp> create_op(const OpSpec& spec);

}  // namespace lark::column::exec
