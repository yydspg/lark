#include "coro/thread_pool.h"

#include <utility>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#endif

namespace lark::coro {

// ─────────────────────────────────────────────────────────────────────────────
// CPU affinity
// ─────────────────────────────────────────────────────────────────────────────
bool PinCurrentThread(int cpu) {
#if defined(__linux__)
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
#elif defined(__APPLE__)
  // THREAD_AFFINITY_POLICY is a hint: the kernel keeps threads tagged with the
  // same affinity tag on the same physical core (no hard pin on macOS).
  thread_affinity_policy_data_t policy = {static_cast<integer_t>(cpu)};
  const thread_port_t port = mach_thread_self();
  const auto kr =
      thread_policy_set(port, THREAD_AFFINITY_POLICY,
                        reinterpret_cast<thread_policy_t>(&policy),
                        THREAD_AFFINITY_POLICY_COUNT);
  mach_port_deallocate(mach_task_self(), port);
  return kr == KERN_SUCCESS;
#else
  (void)cpu;
  return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Defaults / config
// ─────────────────────────────────────────────────────────────────────────────
std::size_t DefaultThreadsFor(PoolKind kind) {
  switch (kind) {
    case PoolKind::kCompute:
      return std::thread::hardware_concurrency();
    case PoolKind::kIo:
      return 2 * std::thread::hardware_concurrency();
    case PoolKind::kLowLoad:
      return 2;
    case PoolKind::kDag:
      return std::thread::hardware_concurrency();
    case PoolKind::kDaemon:
      return 1;
  }
  return 1;
}

namespace {
void ApplyPriority(int /*priority*/) {
#if defined(__linux__)
  // Best-effort scheduling priority (higher = more favorable). Requires
  // privileges for real-time ranges; SCHED_OTHER relative priority is a hint.
  if (priority != 0) {
    sched_param param{};
    param.sched_priority = priority;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
  }
#endif
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
ThreadPool::ThreadPool(size_t thread_count) {
  config_.kind = PoolKind::kCompute;
  config_.threads = thread_count == 0 ? DefaultThreadsFor(config_.kind)
                                      : thread_count;
  if (config_.threads == 0) config_.threads = 1;

  workers_.reserve(config_.threads);
  for (std::size_t i = 0; i < config_.threads; ++i) {
    workers_.emplace_back([this, i] {
      if (!config_.affinity.empty()) {
        PinCurrentThread(config_.affinity[i % config_.affinity.size()]);
      }
      ApplyPriority(config_.priority);
      WorkerLoop();
    });
  }
}

ThreadPool::ThreadPool(const ThreadPoolConfig& config) {
  config_ = config;
  if (config_.threads == 0) config_.threads = DefaultThreadsFor(config_.kind);
  if (config_.threads == 0) config_.threads = 1;

  workers_.reserve(config_.threads);
  for (std::size_t i = 0; i < config_.threads; ++i) {
    workers_.emplace_back([this, i] {
      if (!config_.affinity.empty()) {
        PinCurrentThread(config_.affinity[i % config_.affinity.size()]);
      }
      ApplyPriority(config_.priority);
      WorkerLoop();
    });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

size_t ThreadPool::size() const noexcept { return workers_.size(); }

std::size_t ThreadPool::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

void ThreadPool::Enqueue(std::coroutine_handle<> handle) {
  if (!handle) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config_.max_queue > 0 && queue_.size() >= config_.max_queue) {
      // Backpressure: drop this handle (the task simply does not run).
      return;
    }
    queue_.push_back(handle);
  }
  if (monitor_) {
    monitor_->Emit(::lark::metric::Event{"coro", "pool.enqueue", ""}
                       .attr("pool", std::to_string(workers_.size()))
                       .attr("kind", PoolKindName(config_.kind)));
  }
  cv_.notify_one();
}

void ThreadPool::ScheduleAwaiter::await_suspend(
    std::coroutine_handle<> handle) const noexcept {
  pool_->Enqueue(handle);
}

ThreadPool::ScheduleAwaiter ThreadPool::Schedule() noexcept {
  return ScheduleAwaiter{this};
}

void ThreadPool::WorkerLoop() {
  for (;;) {
    std::coroutine_handle<> handle;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (stop_ && queue_.empty()) {
        return;
      }
      handle = queue_.front();
      queue_.pop_front();
    }
    handle.resume();
  }
}

}  // namespace lark::coro
