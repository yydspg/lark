// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "column/tensor.h"

namespace lark::column {

// TensorTable: a collection of named Tensors (columns) with equal size.
// This is the primary data structure that flows through the computation graph.
class TensorTable {
 public:
  TensorTable() = default;

  // Move-only
  TensorTable(TensorTable&&) noexcept = default;
  TensorTable& operator=(TensorTable&&) noexcept = default;
  TensorTable(const TensorTable&) = delete;
  TensorTable& operator=(const TensorTable&) = delete;

  // Add or replace a tensor
  void add(const std::string& name, Tensor tensor);

  // Set alias for add
  void set(const std::string& name, Tensor tensor) { add(name, std::move(tensor)); }

  // Access
  Tensor& get(const std::string& name);
  const Tensor& get(const std::string& name) const;

  // Check existence
  bool has(const std::string& name) const;

  // Remove
  void remove(const std::string& name);

  // Info
  size_t num_rows() const { return num_rows_; }
  size_t num_cols() const { return tensors_.size(); }
  bool empty() const { return tensors_.empty(); }
  std::vector<std::string> names() const;

  // Deep copy
  TensorTable clone() const;

 private:
  std::unordered_map<std::string, Tensor> tensors_;
  std::vector<std::string> order_;  // insertion order
  size_t num_rows_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Factory: create a TensorTable from row-oriented data
// ─────────────────────────────────────────────────────────────────────────────
inline TensorTable table_from_rows(
    const std::vector<std::string>& col_names,
    std::vector<std::vector<int64_t>> int_cols,
    std::vector<std::vector<double>> dbl_cols = {}) {
  TensorTable table;
  size_t n = 0;

  size_t int_idx = 0, dbl_idx = 0;
  for (const auto& name : col_names) {
    // Determine if this column is int or double based on which list has data
    if (int_idx < int_cols.size() && !int_cols[int_idx].empty()) {
      if (n == 0) n = int_cols[int_idx].size();
      table.add(name, Tensor::from_data(std::move(int_cols[int_idx])));
      ++int_idx;
    } else if (dbl_idx < dbl_cols.size()) {
      if (n == 0) n = dbl_cols[dbl_idx].size();
      table.add(name, Tensor::from_data(std::move(dbl_cols[dbl_idx])));
      ++dbl_idx;
    }
  }
  return table;
}

}  // namespace lark::column
