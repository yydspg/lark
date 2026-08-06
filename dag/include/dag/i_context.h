#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>

namespace lark {

namespace coro {
class ThreadPool;
}  // namespace coro

// Bring the pool type into the lark namespace for convenience.
using ThreadPool = coro::ThreadPool;

// Classification of worker pools. Business code picks the right pool when
// scheduling work so that CPU-bound tasks don't starve I/O-bound ones.
enum class PoolKind {
  kCompute,  // CPU-bound work (no blocking syscalls)
  kIo,       // I/O-bound work (network, disk, sleeps, ...)
  kBackground,  // Low-priority background tasks (logging, metrics, ...)
};

// Abstract interface for the per-request, type-erased data bag shared by all
// nodes.
//
// The framework (Node / Executor) depends only on this interface, so business
// code can plug in any storage strategy by inheriting IContext:
//   * DefaultContext (default_context.h) -- the built-in implementation backed
//     by plain unordered_map + shared_ptr<void> (no internal locking; see
//     concurrency contract below).
//   * A custom subclass -- e.g. a context that forwards to a request-scoped
//     service, a tracing-aware context, a mock for tests, etc.
//
// Concurrency contract for data / domain storage:
//   The framework orchestrates nodes fully asynchronously -- a node's body
//   runs on a worker thread, and any suspension (co_await) resumes on a
//   worker. The thread-pool queue provides the happens-before edge, so a
//   write performed by one node is visible to any node that later awaits its
//   completion event. The framework therefore does NOT take a lock around
//   context reads/writes. Business code MUST ensure that no two coroutines
//   write the same key concurrently; concurrent reads (with or without a
//   concurrent write on a different key) are safe.
//
// Pool access:
//   GetPool(PoolKind) returns a reference to the worker pool of the given
//   class. The pools are owned by the Executor and live for the duration of
//   Execute(); business code uses them to schedule work (see schedule.h).
class IContext {
 public:
  virtual ~IContext() = default;

  // ---- keyed data -------------------------------------------------------
  virtual void SetVoid(const std::string& key,
                       std::shared_ptr<void> value,
                       std::type_index type) = 0;
  virtual std::shared_ptr<const void> GetVoid(
      const std::string& key, std::type_index type) const = 0;
  virtual bool Has(const std::string& key, std::type_index type) const = 0;
  virtual void Erase(const std::string& key) = 0;

  // ---- domain contexts --------------------------------------------------
  virtual void SetDomainVoid(std::type_index type,
                             std::shared_ptr<void> value) = 0;
  virtual std::shared_ptr<const void> GetDomainVoid(
      std::type_index type) const = 0;

  // ---- worker pools -----------------------------------------------------
  // Returns the worker pool of the given class. The pool is owned by the
  // Executor and is valid for the duration of the current Execute() call.
  virtual ThreadPool& GetPool(PoolKind kind) = 0;
};

}  // namespace lark
