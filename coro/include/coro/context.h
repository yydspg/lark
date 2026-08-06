// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Lock-free step context for async orchestration.
//
// Steps exchange data by writing fields into a shared Context instead of
// returning values up a chain. No locking is performed on purpose: the
// business layer guarantees that no two concurrently-running steps write the
// same field (sequential steps never overlap; parallel branches must write
// distinct fields), so there is never contention.

#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace lark::coro {

class Context {
 public:
  Context() = default;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  // Write a field (overwrites any existing value for the key).
  template <typename T>
  void Set(const std::string& key, T value) {
    fields_.insert_or_assign(key, Field{std::make_shared<T>(std::move(value)),
                                        typeid(T)});
  }

  template <typename T>
  const T& Get(const std::string& key) const {
    auto it = fields_.find(key);
    if (it == fields_.end()) {
      throw std::out_of_range("Context: missing field '" + key + "'");
    }
    return *std::static_pointer_cast<const T>(it->second.value);
  }

  template <typename T>
  T& Get(const std::string& key) {
    auto it = fields_.find(key);
    if (it == fields_.end()) {
      throw std::out_of_range("Context: missing field '" + key + "'");
    }
    return *std::static_pointer_cast<T>(it->second.value);
  }

  template <typename T>
  bool Has(const std::string& key) const {
    auto it = fields_.find(key);
    return it != fields_.end() && it->second.type == typeid(T);
  }

  void Erase(const std::string& key) { fields_.erase(key); }
  void Clear() { fields_.clear(); }
  std::size_t size() const { return fields_.size(); }

 private:
  struct Field {
    std::shared_ptr<void> value;
    std::type_index type;
  };

  std::unordered_map<std::string, Field> fields_;
};

}  // namespace lark::coro
