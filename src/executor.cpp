#include "dag/executor.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <thread>

#include "dag/default_context.h"
#include "dag/graph.h"
#include "dag/monitor.h"
#include "dag/node.h"

namespace lark {

using std::atomic;
using std::chrono::nanoseconds;
using std::chrono::steady_clock;
using std::exception_ptr;
using std::future;
using std::lock_guard;
using std::move;
using std::shared_ptr;
using std::size_t;
using std::thread;

// Shared coordination state for a single Execute() call. Lives on the calling
// thread's stack for the duration of the (blocking) call.
struct Executor::RunState {
  atomic<size_t> remaining{0};
  std::promise<void> done;
};

namespace {
size_t Resolve(size_t requested, size_t fallback) {
  return requested != 0 ? requested : fallback;
}
}  // namespace

Executor::Executor(size_t compute_threads, size_t io_threads,
                   size_t background_threads)
    : compute_pool_(Resolve(compute_threads, thread::hardware_concurrency())),
      io_pool_(Resolve(io_threads, 2 * thread::hardware_concurrency())),
      background_pool_(Resolve(background_threads, 2)) {}

Executor::~Executor() = default;

void Executor::SetMonitor(shared_ptr<Monitor> monitor) {
  monitor_ = move(monitor);
}

void Executor::InstallPoolsInto(IContext& ctx) {
  // Only DefaultContext exposes InstallPool; other implementations may ignore.
  if (auto* dctx = dynamic_cast<DefaultContext*>(&ctx)) {
    dctx->InstallPool(PoolKind::kCompute, &compute_pool_);
    dctx->InstallPool(PoolKind::kIo, &io_pool_);
    dctx->InstallPool(PoolKind::kBackground, &background_pool_);
  }
}

coro::FireAndForget Executor::SpawnNode(Node& node, IContext& ctx,
                                        RunState& state) {
  using Clock = steady_clock;

  // Hop onto the IO pool -- most node work is I/O-shaped (fetching data,
  // calling services). Business code that needs the compute pool can reach it
  // via ctx.GetPool(PoolKind::kCompute) inside Run().
  co_await io_pool_.Schedule();

  // Wait for every dependency. They were all spawned already and run
  // concurrently; awaiting suspends this coroutine and frees the worker.
  for (Node* dep : node.dependencies()) {
    co_await dep->done_event();
  }

  const auto start = Clock::now();
  node.SetStatus(NodeStatus::kRunning);
  if (monitor_) {
    monitor_->OnNodeStart(node);
  }

  exception_ptr error;
  try {
    co_await node.Run(ctx);
  } catch (...) {
    error = std::current_exception();
  }

  const auto elapsed = Clock::now() - start;
  node.SetElapsed(elapsed);

  if (!error) {
    node.SetStatus(NodeStatus::kSuccess);
    if (monitor_) {
      monitor_->OnNodeSuccess(node, elapsed);
    }
  } else {
    node.SetError(error);
    bool recovered = false;
    try {
      recovered = node.Fallback(ctx, error);
    } catch (...) {
      recovered = false;
    }
    if (recovered) {
      node.SetStatus(NodeStatus::kFallback);
      if (monitor_) {
        monitor_->OnNodeFallback(node);
      }
    } else {
      node.SetStatus(NodeStatus::kFailed);
      if (monitor_) {
        monitor_->OnNodeFailure(node, error, elapsed);
      }
    }
  }

  // Always unblock dependents so the graph completes deterministically even on
  // failure; downstream nodes can inspect a dependency's status if they care.
  node.done_event().Set();

  if (state.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    state.done.set_value();
  }
  co_return;
}

void Executor::Execute(Graph& graph, IContext& ctx) {
  graph.ResetRunState();
  if (graph.empty()) {
    return;
  }

  // Make the pools reachable from business code for the duration of this call.
  InstallPoolsInto(ctx);

  RunState state;
  state.remaining.store(graph.size(), std::memory_order_relaxed);
  future<void> done = state.done.get_future();

  for (const auto& node : graph.nodes()) {
    SpawnNode(*node, ctx, state);
  }

  done.wait();
}

}  // namespace lark
