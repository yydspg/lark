// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "column/biz/sub_graph.h"

namespace lark::column::biz {

// ─────────────────────────────────────────────────────────────────────────────
// Module: the unit of business computation.
//
// A module only cares about:
//   * its own sub-graph (how IT orchestrates its data),
//   * which columns its data comes from (inputs / ports),
//   * which columns it produces (outputs / ports).
//
// A module is deliberately unaware of:
//   * how it is joined into the global compute graph,
//   * whether the producers of its inputs have finished.
//
// The framework (Pipeline) resolves all of that at compile time using
// anonymous/temp nodes and dependency edges.
// ─────────────────────────────────────────────────────────────────────────────
class Module {
 public:
  explicit Module(std::string name);
  Module(const Module&) = delete;
  Module& operator=(const Module&) = delete;

  // Declare an input port (a column the module reads; produced either by the
  // feed stage or by another module's output).
  Module& input(std::string port);
  // Declare an output port (a column the module produces for consumers).
  Module& output(std::string port);
  // Force an ordering edge onto another module (no data flow required).
  Module& depends_on(std::string module_name);

  // Pure-code orchestration: append a business op to the sub-graph.
  Module& op(exec::OpSpec spec);

  // DSL orchestration: parse the DSL source into the sub-graph.
  Module& from_dsl(const std::string& source);

  // ---- accessors --------------------------------------------------------
  const std::string& name() const noexcept { return name_; }
  const std::vector<std::string>& inputs() const noexcept { return inputs_; }
  const std::vector<std::string>& outputs() const noexcept { return outputs_; }
  const std::vector<std::string>& dependencies() const noexcept {
    return deps_;
  }
  const SubGraph& subgraph() const noexcept { return subgraph_; }
  SubGraph& subgraph() noexcept { return subgraph_; }
  bool has_dsl() const noexcept { return dsl_.has_value(); }
  const std::string& dsl() const { return dsl_ ? *dsl_ : kEmptyDsl; }

 private:
  static const std::string kEmptyDsl;

  std::string name_;
  std::vector<std::string> inputs_;
  std::vector<std::string> outputs_;
  std::vector<std::string> deps_;
  SubGraph subgraph_;
  std::optional<std::string> dsl_;
};

}  // namespace lark::column::biz
