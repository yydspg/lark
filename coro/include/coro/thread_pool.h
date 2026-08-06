#pragma once

#include <coroutine>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "metric/metric.h"

namespace lark::coro {

using std::coroutine_handle;
using std::size_t;

// ─────────────────────────────────────────────────────────────────────────────
// CPU affinity
// ─────────────────────────────────────────────────────────────────────────────

// Best-effort pin of the calling thread to a specific CPU core.
//   Linux: hard pin via sched_setaffinity.
//   macOS: THREAD_AFFINITY_POLICY (a kernel scheduling hint, not a hard pin).
// Returns false when the platform has no usable mechanism.
bool PinCurrentThread(int cpu);

// ─────────────────────────────────────────────────────────────────────────────
// Pool kinds: the framework's differentiated worker pools.
//   kCompute   — CPU-bound work (auto-sized to hardware concurrency)
//   kIo        — I/O-bound work (network, disk, sleeps)
//   kLowLoad   — low-priority background work (2 workers)
//   kDag       — DAG business execution
//   kDaemon    — daemon / maintenance tasks (1 worker)
// ─────────────────────────────────────────────────────────────────────────────
enum class PoolKind { kCompute = 0, kIo, kLowLoad, kDag, kDaemon };

inline const char* PoolKindName(PoolKind kind) noexcept {
  switch (kind) {
    case PoolKind::kCompute:
      return "compute";
    case PoolKind::kIo:
      return "io";
    case PoolKind::kLowLoad:
      return "lowload";
    case PoolKind::kDag:
      return "dag";
    case PoolKind::kDaemon:
      return "daemon";
  }
  return "unknown";
}

// Default (auto-tuned) worker count for a pool kind.
std::size_t DefaultThreadsFor(PoolKind kind);

// ─────────────────────────────────────────────────────────────────────────────
// ThreadPoolConfig: construction + tuning knobs for a worker pool.
//   threads     0 = auto default for the kind
//   affinity    CPUs to pin workers to (round-robin); empty = no pinning
//   priority    scheduling priority hint (>0 higher; best-effort)
//   max_queue   0 = unlimited (tuning knob for backpressure)
// ─────────────────────────────────────────────────────────────────────────────
struct ThreadPoolConfig {
  PoolKind kind = PoolKind::kCompute;
  std::size_t threads = 0;
  std::vector<int> affinity;
  int priority = 0;
  std::size_t max_queue = 0;

  ThreadPoolConfig() = default;
  explicit ThreadPoolConfig(std::size_t threads, PoolKind kind = PoolKind::kCompute)
      : kind(kind), threads(threads) {}
};

// A fixed-size pool of worker threads that resume coroutine handles.
//
// The pool never blocks a worker on a dependency: coroutines that are waiting
// on an AsyncEvent suspend and release their worker, so a small pool can drive
// an arbitrarily wide DAG without starvation.
//
// Optional monitoring: when a monitor is installed (SetMonitor), the pool emits
// "coro" events for each scheduled task ("pool.enqueue", "pool.run",
// "pool.complete"); otherwise the hot path is unchanged (no overhead).
class ThreadPool {
 public:
  explicit ThreadPool(size_t thread_count);
  explicit ThreadPool(const ThreadPoolConfig& config);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  size_t size() const noexcept;
  const ThreadPoolConfig& config() const noexcept { return config_; }

  // Number of coroutines currently waiting in the queue (for load / tuning).
  std::size_t pending() const;

  // Install a unified monitor implementation for scheduling events.
  void SetMonitor(std::shared_ptr<metric::Monitor> monitor) noexcept {
    monitor_ = std::move(monitor);
  }

  // Enqueue a coroutine handle to be resumed on a worker thread.
  void Enqueue(coroutine_handle<> handle);

  // Awaitable that reschedules the awaiting coroutine onto a worker thread.
  //   co_await pool.Schedule();
  class ScheduleAwaiter {
   public:
    explicit ScheduleAwaiter(ThreadPool* pool) noexcept : pool_(pool) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(coroutine_handle<> handle) const noexcept;
    void await_resume() const noexcept {}

   private:
    ThreadPool* pool_;
  };

  ScheduleAwaiter Schedule() noexcept;

 private:
  void WorkerLoop();

  std::vector<std::thread> workers_;
  std::deque<std::coroutine_handle<>> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
  std::shared_ptr<metric::Monitor> monitor_;
  ThreadPoolConfig config_;
};

}  // namespace lark::coro
