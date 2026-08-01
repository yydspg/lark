#pragma once

#include <atomic>
#include <chrono>
#include <exception>
#include <string>
#include <vector>

#include "dag/i_context.h"
#include "dag/coro/async_event.h"
#include "dag/coro/task.h"

namespace lark {

using std::atomic;
using std::chrono::nanoseconds;
using std::exception_ptr;
using std::string;
using std::vector;
using coro::AsyncEvent;
using coro::Task;

// Lifecycle state of a node within a single graph execution.
enum class NodeStatus {
  kPending,   // not started
  kRunning,   // executing
  kSuccess,   // completed normally
  kFailed,    // threw and had no (or failed) fallback
  kFallback,  // threw but fallback() produced a degraded result
};

// Human-readable name for a status (for logs / monitoring).
const char* ToString(NodeStatus status) noexcept;

// Abstract base for all business nodes.
//
// Business code subclasses Node and implements Compute() for synchronous work,
// or overrides Run() to perform fully asynchronous work (it returns a
// coroutine Task and may co_await other async operations). The framework owns
// the node, drives it once its dependencies complete, times it, catches
// failures and invokes Fallback() for graceful degradation ("兜底").
//
// Framework-managed state (id, dependencies, completion event, status) is set
// by the Graph/Executor; business code only reads it.
class Node {
 public:
  Node();
  virtual ~Node();

  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  // ---- business extension points ---------------------------------------

  // Synchronous business logic. Default no-op; override for the common case.
  virtual void Compute(IContext& ctx);

  // Asynchronous entry point. The default implementation simply runs Compute()
  // and completes. Override to await other async work; the returned Task is
  // co_await-ed by the executor.
  virtual Task<void> Run(IContext& ctx);

  // Fallback / degradation hook, invoked by the executor when Run() throws.
  // Return true if a usable degraded result was produced (status becomes
  // kFallback); return false to propagate failure (status becomes kFailed).
  virtual bool Fallback(IContext& ctx, exception_ptr error);

  // ---- framework-managed accessors --------------------------------------
  const string& id() const noexcept;
  const string& type() const noexcept;
  const vector<Node*>& dependencies() const noexcept;
  NodeStatus status() const noexcept;
  nanoseconds elapsed() const noexcept;
  exception_ptr error() const noexcept;

  // ---- framework internals (called by Graph/Executor) -------------------
  void SetIdentity(string id, string type);
  void AddDependency(Node* dep);
  AsyncEvent& done_event() noexcept;

  void SetStatus(NodeStatus status);
  void SetElapsed(nanoseconds elapsed);
  void SetError(exception_ptr error);

  // Clear per-run state so the node can be executed again (graph reuse).
  void ResetRunState();

 private:
  string id_;
  string type_;
  vector<Node*> dependencies_;

  AsyncEvent done_event_;
  atomic<NodeStatus> status_;
  nanoseconds elapsed_;
  exception_ptr error_;
};

}  // namespace lark
