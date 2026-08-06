// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Op registry — Spring-IOC-like class collection for dag::Op.
//
// Op classes register themselves by type name at static-init time (LARK_OP
// macro). The DSL / KV graph builders then instantiate ops by type name, so a
// graph can be described declaratively without touching the classes:
//
//   class DataFetchOp : public dag::Op { ... };
//   LARK_OP("DataFetchOp", DataFetchOp);

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dag/op.h"

namespace lark::dag {

using OpFactory = std::function<std::unique_ptr<Op>()>;

class OpRegistry {
 public:
  // Process-wide registry used by the LARK_OP macro.
  static OpRegistry& Instance();

  void Register(std::string type_name, OpFactory factory);
  bool Contains(const std::string& type_name) const;

  // Create a fresh op of `type_name`. `id` is the graph node id (informational;
  // the op itself carries its business Name()). Throws on unknown type.
  std::unique_ptr<Op> Create(const std::string& type_name,
                             const std::string& id = {}) const;

  std::vector<std::string> RegisteredTypes() const;

 private:
  OpRegistry() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, OpFactory> factories_;
};

}  // namespace lark::dag

// Auto-registration: registers `CLASS` under `NAME` in the process-wide
// OpRegistry at static-init time.
#define LARK_OP(NAME, CLASS)                                                 \
  namespace {                                                                \
  [[maybe_unused]] const bool kLarkOpRegistered_##CLASS = [] {               \
    ::lark::dag::OpRegistry::Instance().Register(                            \
        (NAME), [] { return std::make_unique<CLASS>(); });                   \
    return true;                                                             \
  }();                                                                       \
  }
