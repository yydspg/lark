#include "dag/executor.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "dag/default_context.h"
#include "dag/graph.h"
#include "dag/node.h"
#include "metric/metric.h"

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
using std::string;
using std::thread;
using std::unordered_set;
using std::vector;

using Clock = steady_clock;

namespace {

size_t Resolve(size_t requested, size_t fallback) {
  return requested != 0 ? requested : fallback;
}

// Best-effort textual description of an exception for monitoring events.
std::string DescribeError(const exception_ptr& error) {
  if (!error) return "";
  try {
    std::rethrow_exception(error);
  } catch (const std::exception& e) {
    return e.what();
  } catch (...) {
    return "unknown error";
  }
}

// Build a node event with the common per-node attributes.
metric::Event NodeEvent(const Node& node, const char* action,
                         const char* status, nanoseconds elapsed,
                         nanoseconds started, const std::string& error = {}) {
  metric::Event e{"dag", action, node.id()};
  e.attr("type", node.type()).attr("status", status);
  if (elapsed.count() >= 0) e.attr_ns("elapsed_ns", elapsed.count());
  e.attr_ns("started_ns", started.count());
  if (!error.empty()) e.attr("error", error);
  e.duration = elapsed;
  e.ok = error.empty();
  return e;
}

}  // namespace

// Shared coordination state for a single Execute() call. Lives on the calling
// thread's stack for the duration of the (blocking) call.
struct Executor::RunState {
  atomic<size_t> remaining{0};
  std::promise<void> done;
  steady_clock::time_point run_start{};
};

Executor::Executor(size_t compute_threads, size_t io_threads,
                   size_t background_threads)
    : compute_pool_(Resolve(compute_threads, thread::hardware_concurrency())),
      io_pool_(Resolve(io_threads, 2 * thread::hardware_concurrency())),
      background_pool_(Resolve(background_threads, 2)) {}

Executor::~Executor() = default;

void Executor::SetMonitor(shared_ptr<metric::Monitor> monitor) {
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
  node.SetStartedAt(start - state.run_start);
  node.SetStatus(NodeStatus::kRunning);
  if (monitor_) {
    monitor_->Emit(NodeEvent(node, "node.start", "running", nanoseconds{-1},
                             node.started_at()));
  }

  exception_ptr error;
  std::string error_msg;
  try {
    co_await node.Run(ctx);
  } catch (...) {
    error = std::current_exception();
    error_msg = DescribeError(error);
  }

  const auto elapsed = Clock::now() - start;
  node.SetElapsed(elapsed);

  if (!error) {
    node.SetStatus(NodeStatus::kSuccess);
    if (monitor_) {
      monitor_->Emit(NodeEvent(node, "node.success", "success", elapsed,
                               node.started_at()));
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
        monitor_->Emit(NodeEvent(node, "node.fallback", "fallback", elapsed,
                                 node.started_at()));
      }
    } else {
      node.SetStatus(NodeStatus::kFailed);
      if (monitor_) {
        monitor_->Emit(NodeEvent(node, "node.failure", "failed", elapsed,
                                 node.started_at(), error_msg));
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

void Executor::Execute(Graph& graph, IContext& ctx,
                       const vector<string>& disabled_ids) {
  graph.ResetRunState();
  if (graph.empty()) {
    return;
  }

  // Make the pools reachable from business code for the duration of this call.
  InstallPoolsInto(ctx);

  RunState state;
  state.remaining.store(graph.size(), std::memory_order_relaxed);
  state.run_start = Clock::now();
  future<void> done = state.done.get_future();

  unordered_set<string> disabled(disabled_ids.begin(), disabled_ids.end());

  for (const auto& node : graph.nodes()) {
    if (disabled.count(node->id()) > 0) {
      // Batch-disable: skip execution, fire completion immediately so the
      // graph drains without blocking. Counted toward completion below.
      node->SetStatus(NodeStatus::kSkipped);
      if (monitor_) {
        monitor_->Emit(NodeEvent(*node, "node.skipped", "skipped",
                                 nanoseconds::zero(),
                                 node->started_at()));
      }
      node->done_event().Set();
      if (state.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        state.done.set_value();
      }
      continue;
    }
    SpawnNode(*node, ctx, state);
  }

  done.wait();
}

}  // namespace lark
