// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <string>

#include "column/node/node.h"

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// AggregateNode: business node that reduces the table to a single row.
//
// The user-supplied function receives the full table and returns a new
// single-row TensorTable with the aggregated results.
// ─────────────────────────────────────────────────────────────────────────────
class AggregateNode : public Node {
 public:
  using AggregateFn = std::function<TensorTable(const TensorTable&)>;

  explicit AggregateNode(AggregateFn fn);

  TensorTable execute(TensorTable input) const override;
  const char* node_type() const noexcept override;

 private:
  AggregateFn fn_;
};

}  // namespace lark::column
