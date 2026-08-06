// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "column/exec/tensor_op.h"
#include "column/types.h"

namespace lark::column::backend {

// ─────────────────────────────────────────────────────────────────────────────
// Backend: abstracts a compute backend that materializes TensorOps.
//
// Only the CPU backend is implemented in this codebase. The abstraction makes
// it possible to add other backends later (e.g. GPU / accelerator) without
// touching the business layer: modules are written against ops, and the
// backend factory decides how each op runs.
// ─────────────────────────────────────────────────────────────────────────────
class Backend {
 public:
  virtual ~Backend() = default;

  virtual const char* name() const noexcept = 0;

  // Materialize an execution op from its spec. Throws std::invalid_argument on
  // an op the backend cannot produce.
  virtual std::unique_ptr<exec::TensorOp> CreateOp(const exec::OpSpec& spec) = 0;

  // Whether the backend can compute over a given element type.
  virtual bool SupportsDType(DType dtype) const noexcept = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// CpuBackend: the built-in, scalar/SIMD CPU implementation (ggml-style).
// ─────────────────────────────────────────────────────────────────────────────
class CpuBackend final : public Backend {
 public:
  const char* name() const noexcept override { return "cpu"; }

  std::unique_ptr<exec::TensorOp> CreateOp(const exec::OpSpec& spec) override {
    return exec::create_op(spec);
  }

  bool SupportsDType(DType dtype) const noexcept override {
    return is_scalar_type(dtype) || is_quantized(dtype);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// BackendFactory: a registry of backend factories keyed by name.
//
// The "cpu" backend is auto-registered at startup. Business code picks a
// backend once per pipeline:
//
//   auto backend = backend::BackendFactory::Instance().Create("cpu");
//   Pipeline pipeline(backend);
// ─────────────────────────────────────────────────────────────────────────────
using BackendFactoryFn = std::function<std::unique_ptr<Backend>()>;

class BackendFactory {
 public:
  // Process-wide registry used by business code.
  static BackendFactory& Instance();

  void Register(std::string name, BackendFactoryFn factory);

  // Create a backend by name. Throws std::out_of_range on an unknown name.
  std::unique_ptr<Backend> Create(const std::string& name) const;
  bool Contains(const std::string& name) const;
  std::vector<std::string> Available() const;

 private:
  BackendFactory();

  mutable std::mutex mutex_;
  std::unordered_map<std::string, BackendFactoryFn> factories_;
};

// Convenience: create the built-in CPU backend directly.
std::unique_ptr<Backend> CreateCpuBackend();

}  // namespace lark::column::backend
