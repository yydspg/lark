#pragma once

#include <coroutine>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace lark::coro {

using std::coroutine_handle;
using std::size_t;

// A fixed-size pool of worker threads that resume coroutine handles.
//
// The pool never blocks a worker on a dependency: coroutines that are waiting
// on an AsyncEvent suspend and release their worker, so a small pool can drive
// an arbitrarily wide DAG without starvation.
class ThreadPool {
 public:
  explicit ThreadPool(size_t thread_count);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  size_t size() const noexcept;

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
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
};

}  // namespace lark::coro
