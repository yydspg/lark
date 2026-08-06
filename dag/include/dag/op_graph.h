// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// A graph of business Ops with dependency edges and aspects.

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "dag/op.h"

namespace lark::dag {

class OpGraph {
 public:
  OpGraph() = default;
  OpGraph(const OpGraph&) = delete;
  OpGraph& operator=(const OpGraph&) = delete;

  // Add an op under a node id. `id` defaults to the op's Name(). Multiple ops
  // of the same type may be added under distinct ids.
  OpGraph& AddOp(std::shared_ptr<Op> op, std::string id = {});
  // Attach a cross-cutting aspect applied around every op.
  OpGraph& AddAspect(std::shared_ptr<OpAspect> aspect);
  // `dep` must finish before `dependent` runs. Both must already be added.
  OpGraph& DependsOn(const std::string& dep, const std::string& dependent);

  // Validate and produce a topological order. Throws on unknown names / cycles.
  // Called automatically by the executor; safe to call repeatedly.
  const std::vector<Op*>& Compile();

  const std::vector<Op*>& ops() const { return order_; }
  const std::vector<Op*>& DepsOf(const Op* op) const {
    auto it = deps_.find(op);
    return it == deps_.end() ? kEmptyDeps : it->second;
  }
  const std::vector<std::shared_ptr<OpAspect>>& aspects() const {
    return aspects_;
  }
  std::size_t size() const { return ops_.size(); }
  bool compiled() const { return compiled_; }

 private:
  static const std::vector<Op*> kEmptyDeps;

  std::vector<std::shared_ptr<Op>> ops_;
  std::unordered_map<std::string, Op*> by_id_;
  std::unordered_map<const Op*, std::vector<Op*>> deps_;
  std::vector<std::shared_ptr<OpAspect>> aspects_;
  std::vector<Op*> order_;
  bool compiled_ = false;
};

}  // namespace lark::dag
