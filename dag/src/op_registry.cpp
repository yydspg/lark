// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "dag/op_registry.h"

#include <stdexcept>

namespace lark::dag {

OpRegistry& OpRegistry::Instance() {
  static OpRegistry registry;
  return registry;
}

void OpRegistry::Register(std::string type_name, OpFactory factory) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!factories_.emplace(std::move(type_name), std::move(factory)).second) {
    throw std::invalid_argument("OpRegistry: duplicate op type");
  }
}

bool OpRegistry::Contains(const std::string& type_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.find(type_name) != factories_.end();
}

std::unique_ptr<Op> OpRegistry::Create(const std::string& type_name,
                                       const std::string&) const {
  OpFactory factory;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(type_name);
    if (it == factories_.end()) {
      throw std::out_of_range("OpRegistry: unknown op type '" + type_name +
                              "'");
    }
    factory = it->second;
  }
  return factory();
}

std::vector<std::string> OpRegistry::RegisteredTypes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> types;
  types.reserve(factories_.size());
  for (const auto& [name, _] : factories_) types.push_back(name);
  return types;
}

}  // namespace lark::dag
