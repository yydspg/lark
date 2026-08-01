// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/node/aggregate_node.h"

namespace lark::column {

AggregateNode::AggregateNode(AggregateFn fn) : fn_(std::move(fn)) {}

TensorTable AggregateNode::execute(TensorTable input) const {
  return fn_(input);
}

const char* AggregateNode::node_type() const noexcept { return "AggregateNode"; }

}  // namespace lark::column
