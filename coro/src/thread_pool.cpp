#include "coro/thread_pool.h"

#include <utility>

namespace lark::coro {

ThreadPool::ThreadPool(size_t thread_count) {
  if (thread_count == 0) {
    thread_count = 1;
  }
  workers_.reserve(thread_count);
  for (size_t i = 0; i < thread_count; ++i) {
    workers_.emplace_back([this] { WorkerLoop(); });
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

size_t ThreadPool::size() const noexcept {
  return workers_.size();
}

void ThreadPool::Enqueue(std::coroutine_handle<> handle) {
  if (!handle) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(handle);
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
