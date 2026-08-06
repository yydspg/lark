// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/backend/backend.h"

#include <stdexcept>

namespace lark::column::backend {

BackendFactory::BackendFactory() {
  factories_.emplace("cpu", [] { return std::make_unique<CpuBackend>(); });
}

BackendFactory& BackendFactory::Instance() {
  static BackendFactory instance;
  return instance;
}

void BackendFactory::Register(std::string name, BackendFactoryFn factory) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!factories_.emplace(std::move(name), std::move(factory)).second) {
    throw std::invalid_argument("BackendFactory: duplicate backend name");
  }
}

std::unique_ptr<Backend> BackendFactory::Create(const std::string& name) const {
  BackendFactoryFn factory;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(name);
    if (it == factories_.end()) {
      throw std::out_of_range("BackendFactory: unknown backend '" + name + "'");
    }
    factory = it->second;
  }
  return factory();
}

bool BackendFactory::Contains(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.find(name) != factories_.end();
}

std::vector<std::string> BackendFactory::Available() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> names;
  names.reserve(factories_.size());
  for (const auto& [name, _] : factories_) names.push_back(name);
  return names;
}

std::unique_ptr<Backend> CreateCpuBackend() {
  return std::make_unique<CpuBackend>();
}

}  // namespace lark::column::backend
