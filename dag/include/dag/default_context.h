#pragma once

#include <array>
#include <unordered_map>

#include "dag/context.h"
#include "coro/thread_pool.h"

namespace lark {

// Default implementation of IContext.
//
// Storage strategy:
//   * keyed data  -> std::unordered_map<string, Entry>. Values are held as
//                    shared_ptr<void> so ownership is reference-counted and
//                    released deterministically when the context and every
//                    retrieved handle go away -- no leaks.
//   * domain ctxs -> std::unordered_map<type_index, shared_ptr<void>>, one
//                    entry per domain type.
//
// No internal locking: the framework's coroutine scheduling provides the
// happens-before edges (see IContext contract). Business code must ensure no
// two coroutines write the same key concurrently.
//
// Worker pools:
//   The Executor installs three pools (Compute / IO / Background) before
//   running the graph. GetPool(PoolKind) returns a reference to the
//   corresponding pool; business code uses them via the orchestration helpers
//   in schedule.h.
class DefaultContext final : public IContext {
 public:
  DefaultContext() = default;
  DefaultContext(const DefaultContext&) = delete;
  DefaultContext& operator=(const DefaultContext&) = delete;

  // ---- IContext ---------------------------------------------------------
  void SetVoid(const string& key, shared_ptr<void> value,
               type_index type) override;
  shared_ptr<const void> GetVoid(const string& key,
                                 type_index type) const override;
  bool Has(const string& key, type_index type) const override;
  void Erase(const string& key) override;

  void SetDomainVoid(type_index type, shared_ptr<void> value) override;
  shared_ptr<const void> GetDomainVoid(type_index type) const override;

  ThreadPool& GetPool(PoolKind kind) override;

  // ---- pool installation (called by Executor) ---------------------------
  void InstallPool(PoolKind kind, ThreadPool* pool);

  // ---- typed convenience (mirrors the free functions in context.h) ------
  template <typename T>
  void Set(const string& key, T value) {
    lark::Set<T>(*this, key, std::move(value));
  }

  template <typename T>
  shared_ptr<T> Get(const string& key) const {
    return lark::Get<T>(*this, key);
  }

  template <typename T>
  bool Has(const string& key) const {
    return lark::Has<T>(*this, key);
  }

  template <typename T, typename... Args>
  shared_ptr<T> ProvideDomain(Args&&... args) {
    return lark::ProvideDomain<T>(*this, std::forward<Args>(args)...);
  }

  template <typename T>
  shared_ptr<T> Domain() const {
    return lark::Domain<T>(*this);
  }

  template <typename T>
  T& RequireDomain() const {
    return lark::RequireDomain<T>(*this);
  }

 private:
  struct Entry {
    type_index type;
    shared_ptr<void> value;
  };

  std::unordered_map<string, Entry> data_;
  std::unordered_map<type_index, shared_ptr<void>> domains_;
  std::array<ThreadPool*, 3> pools_{};  // indexed by PoolKind
};

}  // namespace lark
