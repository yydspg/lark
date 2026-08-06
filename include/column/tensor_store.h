// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "column/tensor.h"
#include "column/tensor_table.h"

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// TensorStore: the execution-layer data store for graph intermediates.
//
// Unlike TensorTable, a store does NOT enforce a uniform row count: ops such
// as filter/reduce legitimately produce tensors of different lengths, so the
// store is simply a name -> Tensor map (TensorFlow-style "symbol table").
//
// The framework (pipeline / execution context) seeds the store with the fed
// columns, ops write their outputs into it, and fetch reads the requested
// names back out.
// ─────────────────────────────────────────────────────────────────────────────
class TensorStore {
 public:
  TensorStore() = default;

  // Move-only (use clone() for a deep copy).
  TensorStore(TensorStore&&) noexcept = default;
  TensorStore& operator=(TensorStore&&) noexcept = default;
  TensorStore(const TensorStore&) = delete;
  TensorStore& operator=(const TensorStore&) = delete;

  // Insert or replace a tensor. During parallel graph execution every name the
  // graph may write MUST be pre-registered (see EnsureNames) so set() only
  // touches existing elements — concurrent writers never insert/rehash.
  void set(const std::string& name, Tensor tensor);
  // Alias for set().
  void add(const std::string& name, Tensor tensor) {
    set(name, std::move(tensor));
  }

  // Pre-register names with empty placeholder tensors (once, single-threaded,
  // before a parallel run). This stabilizes the map so concurrent op writes
  // are data-race free.
  void EnsureNames(const std::vector<std::string>& names);

  // Const access; throws std::out_of_range if the name is absent.
  const Tensor& get(const std::string& name) const;
  Tensor& get(const std::string& name);

  const Tensor* find(const std::string& name) const;
  bool has(const std::string& name) const;

  void erase(const std::string& name);
  void clear();

  // Names in insertion order.
  std::vector<std::string> names() const { return order_; }
  size_t size() const { return tensors_.size(); }

  // Merge all columns of a TensorTable into the store.
  void add_table(const TensorTable& table);

  // Materialize the requested columns as an equal-length TensorTable.
  // The columns must exist and share the same row count.
  TensorTable to_table(const std::vector<std::string>& names) const;

  // Deep copy.
  TensorStore clone() const;

 private:
  std::unordered_map<std::string, Tensor> tensors_;
  std::vector<std::string> order_;
};

}  // namespace lark::column
