// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Declarative graph building for dag::Op — KV form and a small DSL.
//
// Ops are collected by type name via OpRegistry (LARK_OP macro, Spring-IOC
// style); the builder instantiates them by name, so graphs can be described
// without touching the op classes. The DSL form suits simple, linear flows:
//
//   fetch:DataFetchOp -> select:SelectOp -> rank:RankOp
//   threshold:ConstOp  -> rank:RankOp          // fan-in by reusing the id
//
// KV form (same as the NodeDef style):
//   {{"DataFetchOp"}, {"SelectOp", {"fetch"}, "select"}, ...}

#include <memory>
#include <string>
#include <vector>

#include "dag/op.h"
#include "dag/op_graph.h"
#include "dag/op_registry.h"

namespace lark::dag {

// KV-style node description: `id : registeredType`.
struct OpDef {
  std::string type;               // registered op type (e.g. "SelectOp")
  std::vector<std::string> deps;  // dependency node ids
  std::string id;                 // node id; defaults to `type` when empty

  OpDef() = default;
  OpDef(std::string type, std::vector<std::string> deps = {},
        std::string id = {})
      : type(std::move(type)),
        deps(std::move(deps)),
        id(std::move(id)) {}
};

class OpGraphBuilder {
 public:
  explicit OpGraphBuilder(const OpRegistry& registry = OpRegistry::Instance())
      : registry_(registry) {}

  OpGraphBuilder& AddAspect(std::shared_ptr<OpAspect> aspect) {
    if (aspect) aspects_.push_back(std::move(aspect));
    return *this;
  }

  // KV form: build a graph from node definitions.
  std::shared_ptr<OpGraph> Build(const std::vector<OpDef>& defs) const;

  // DSL form: "id:Type -> id:Type -> ..." (statements separated by ';' or
  // newline; reusing a node id merges its dependencies — fan-in).
  std::shared_ptr<OpGraph> BuildDsl(const std::string& dsl) const;

 private:
  const OpRegistry& registry_;
  std::vector<std::shared_ptr<OpAspect>> aspects_;
};

}  // namespace lark::dag
