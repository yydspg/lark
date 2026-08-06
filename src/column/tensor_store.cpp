// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/tensor_store.h"

#include <algorithm>
#include <stdexcept>

namespace lark::column {

void TensorStore::set(const std::string& name, Tensor tensor) {
  auto it = tensors_.find(name);
  if (it == tensors_.end()) {
    // Insertion path: only used when not running concurrently (seeding).
    order_.push_back(name);
    tensors_.emplace(name, std::move(tensor));
    return;
  }
  // Existing element: replace the value in place — safe under concurrent
  // writers as long as each name is only written by one producer.
  it->second = std::move(tensor);
}

void TensorStore::EnsureNames(const std::vector<std::string>& names) {
  for (const auto& name : names) {
    if (tensors_.find(name) == tensors_.end()) {
      order_.push_back(name);
      tensors_.emplace(name, Tensor());
    }
  }
}

const Tensor& TensorStore::get(const std::string& name) const {
  auto it = tensors_.find(name);
  if (it == tensors_.end()) {
    throw std::out_of_range("TensorStore::get: unknown tensor '" + name + "'");
  }
  return it->second;
}

Tensor& TensorStore::get(const std::string& name) {
  auto it = tensors_.find(name);
  if (it == tensors_.end()) {
    throw std::out_of_range("TensorStore::get: unknown tensor '" + name + "'");
  }
  return it->second;
}

const Tensor* TensorStore::find(const std::string& name) const {
  auto it = tensors_.find(name);
  return it == tensors_.end() ? nullptr : &it->second;
}

bool TensorStore::has(const std::string& name) const {
  return tensors_.find(name) != tensors_.end();
}

void TensorStore::erase(const std::string& name) {
  auto it = tensors_.find(name);
  if (it == tensors_.end()) return;
  tensors_.erase(it);
  order_.erase(std::find(order_.begin(), order_.end(), name));
}

void TensorStore::clear() {
  tensors_.clear();
  order_.clear();
}

void TensorStore::add_table(const TensorTable& table) {
  for (const auto& name : table.names()) {
    set(name, table.get(name).clone());
  }
}

TensorTable TensorStore::to_table(const std::vector<std::string>& names) const {
  TensorTable out;
  size_t rows = 0;
  bool first = true;
  for (const auto& name : names) {
    const Tensor* t = find(name);
    if (t == nullptr) {
      throw std::out_of_range("TensorStore::to_table: unknown tensor '" + name +
                              "'");
    }
    if (first) {
      rows = t->size();
      first = false;
    } else if (t->size() != rows) {
      throw std::runtime_error(
          "TensorStore::to_table: length mismatch for column '" + name + "'");
    }
    out.add(name, t->clone());
  }
  return out;
}

TensorStore TensorStore::clone() const {
  TensorStore result;
  for (const auto& name : order_) {
    result.set(name, tensors_.at(name).clone());
  }
  return result;
}

}  // namespace lark::column
