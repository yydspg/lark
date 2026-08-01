#include "dag/default_context.h"

#include <stdexcept>

namespace lark {

namespace {
// Map PoolKind to the index used by pools_.
constexpr std::size_t IndexOf(PoolKind kind) {
  return static_cast<std::size_t>(kind);
}
}  // namespace

// ---------------------------------------------------------------------------
// Keyed data
// ---------------------------------------------------------------------------
void DefaultContext::SetVoid(const string& key, shared_ptr<void> value,
                             type_index type) {
  data_.insert_or_assign(key, Entry{type, std::move(value)});
}

shared_ptr<const void> DefaultContext::GetVoid(const string& key,
                                               type_index type) const {
  auto it = data_.find(key);
  if (it == data_.end() || it->second.type != type) {
    return nullptr;
  }
  return it->second.value;
}

bool DefaultContext::Has(const string& key, type_index type) const {
  auto it = data_.find(key);
  return it != data_.end() && it->second.type == type;
}

void DefaultContext::Erase(const string& key) { data_.erase(key); }

// ---------------------------------------------------------------------------
// Domain contexts
// ---------------------------------------------------------------------------
void DefaultContext::SetDomainVoid(type_index type, shared_ptr<void> value) {
  domains_.insert_or_assign(type, std::move(value));
}

shared_ptr<const void> DefaultContext::GetDomainVoid(type_index type) const {
  auto it = domains_.find(type);
  if (it == domains_.end()) {
    return nullptr;
  }
  return it->second;
}

// ---------------------------------------------------------------------------
// Worker pools
// ---------------------------------------------------------------------------
void DefaultContext::InstallPool(PoolKind kind, ThreadPool* pool) {
  pools_[IndexOf(kind)] = pool;
}

ThreadPool& DefaultContext::GetPool(PoolKind kind) {
  ThreadPool* pool = pools_[IndexOf(kind)];
  if (pool == nullptr) {
    throw std::runtime_error(
        "DefaultContext::GetPool: pool not installed for requested kind");
  }
  return *pool;
}

}  // namespace lark
