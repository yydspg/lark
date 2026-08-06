// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/tensor_table.h"

#include <algorithm>

namespace lark::column {

void TensorTable::add(const std::string& name, Tensor tensor) {
  size_t sz = tensor.size();
  if (!tensors_.empty() && sz != num_rows_) {
    // Allow if table is empty or sizes match
    if (num_rows_ > 0 && sz != num_rows_) {
      throw std::runtime_error("Tensor size mismatch: expected " +
                               std::to_string(num_rows_) + ", got " +
                               std::to_string(sz));
    }
  }

  if (tensors_.find(name) == tensors_.end()) {
    order_.push_back(name);
  }
  tensors_[name] = std::move(tensor);
  num_rows_ = sz;
}

Tensor& TensorTable::get(const std::string& name) {
  auto it = tensors_.find(name);
  if (it == tensors_.end()) {
    throw std::runtime_error("Tensor not found: " + name);
  }
  return it->second;
}

const Tensor& TensorTable::get(const std::string& name) const {
  auto it = tensors_.find(name);
  if (it == tensors_.end()) {
    throw std::runtime_error("Tensor not found: " + name);
  }
  return it->second;
}

bool TensorTable::has(const std::string& name) const {
  return tensors_.find(name) != tensors_.end();
}

void TensorTable::remove(const std::string& name) {
  auto it = tensors_.find(name);
  if (it == tensors_.end()) return;
  tensors_.erase(it);
  order_.erase(std::find(order_.begin(), order_.end(), name));
  if (tensors_.empty()) {
    num_rows_ = 0;
  }
}

std::vector<std::string> TensorTable::names() const { return order_; }

TensorTable TensorTable::clone() const {
  TensorTable result;
  for (const auto& name : order_) {
    result.add(name, tensors_.at(name).clone());
  }
  return result;
}

}  // namespace lark::column
