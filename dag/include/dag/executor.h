#pragma once

#include <cstddef>
#include <memory>

#include "coro/fire_and_forget.h"
#include "coro/thread_pool.h"
#include "dag/i_context.h"

namespace lark {

using std::shared_ptr;
using std::size_t;

class Graph;
class Monitor;
class Node;

// Drives a Graph to completion with fully asynchronous, coroutine-based
// scheduling on worker pools.
//
// Model: one detached coroutine is spawned per node. Each coroutine awaits the
// completion events of its dependencies, then runs the node on a worker
// thread. Because all node coroutines are launched up front and awaiting a
// dependency *suspends* (releasing the worker), every node whose dependencies
// are ready runs concurrently -- in particular, all inputs of a node execute
// in parallel -- while a small pool never deadlocks.
//
// Three pools are exposed to business code:
//   * Compute pool    -- sized to hardware_concurrency by default; for
//                        CPU-bound work (no blocking syscalls).
//   * IO pool         -- sized to 2 * hardware_concurrency by default; for
//                        I/O-bound work (network, disk, sleeps).
//   * Background pool -- sized to 2 by default; for low-priority background
//                        tasks (logging, metrics, tracing).
// Nodes run on the IO pool by default (most node work is I/O-shaped). Business
// code can schedule additional work on any pool via ctx.GetPool(PoolKind) or
// the helpers in schedule.h.
//
// Execute() blocks the calling thread until every node has finished, which
// guarantees the Graph and Context outlive all coroutine frames (no dangling
// references, no leaks).
class Executor {
 public:
  explicit Executor(size_t compute_threads = 0,
                    size_t io_threads = 0,
                    size_t background_threads = 0);
  ~Executor();

  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  // Optional observability hook. Not owned beyond the shared_ptr lifetime.
  void SetMonitor(shared_ptr<Monitor> monitor);

  // Run the graph to completion (blocking). Safe to call repeatedly and from
  // different threads is not intended; call once per request per graph.
  //
  // `disabled_ids` is the batch-disable set: the listed nodes are NOT executed
  // for this run — each gets NodeStatus::kSkipped, its completion event fires
  // immediately, and downstream nodes proceed as usual. Disabled nodes still
  // count toward graph completion, so Execute() never blocks. (Consumers that
  // depend on a disabled node's produced data are the caller's responsibility.)
  void Execute(Graph& graph, IContext& ctx,
               const std::vector<std::string>& disabled_ids = {});

  size_t compute_worker_count() const noexcept { return compute_pool_.size(); }
  size_t io_worker_count() const noexcept { return io_pool_.size(); }
  size_t background_worker_count() const noexcept { return background_pool_.size(); }

 private:
  struct RunState;

  coro::FireAndForget SpawnNode(Node& node, IContext& ctx, RunState& state);

  // Install the pools into `ctx` so business code can reach them via
  // IContext::GetPool. Only DefaultContext is supported; other IContext
  // implementations may ignore the call.
  void InstallPoolsInto(IContext& ctx);

  coro::ThreadPool compute_pool_;
  coro::ThreadPool io_pool_;
  coro::ThreadPool background_pool_;
  shared_ptr<Monitor> monitor_;
};

}  // namespace lark
