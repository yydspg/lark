// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Compute auto-tuning.
//
// Decides, per task, whether to run as a coroutine on the compute pool, on a
// dedicated thread, or on a dedicated CPU-pinned thread — based on the task's
// estimated duration, whether it is CPU-bound, and the pool's current load.
// This implements "auto tune: thread or coroutine, and pin super-heavy CPU
// tasks".

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>

#include "coro/future.h"
#include "coro/thread_pool.h"

namespace lark::coro {

// Execution strategy chosen by the tuner / requested by the caller.
enum class ExecMode {
  kAuto,          // let the tuner decide
  kCoroutine,     // cooperative: run as a coroutine on the compute pool
  kThread,        // dedicated std::thread
  kPinnedThread,  // dedicated thread pinned to a CPU (super-heavy tasks)
};

struct ComputeTunerOptions {
  // CPU-bound task longer than this -> dedicated thread (default 1ms).
  int64_t thread_above_ns = 1'000'000;
  // CPU-bound task longer than this -> pinned dedicated thread (default 50ms).
  int64_t pinned_above_ns = 50'000'000;
  // When the compute pool load exceeds this, offload heavy work to threads.
  double max_pool_utilization = 0.8;
};

// Pure decision logic: given task characteristics + pool load, pick an
// execution mode.
class ComputeTuner {
 public:
  explicit ComputeTuner(ComputeTunerOptions options = {}) : options_(options) {}

  ExecMode Decide(int64_t estimated_ns, bool cpu_bound, double pool_utilization,
                  ExecMode requested = ExecMode::kAuto) const;

 private:
  ComputeTunerOptions options_;
};

// Executes a callable honoring the tuner's decision.
class ComputeExecutor {
 public:
  explicit ComputeExecutor(ComputeTuner tuner = ComputeTuner())
      : tuner_(std::move(tuner)) {}

  // Run `work` (a void() callable) with automatic thread-vs-coroutine choice.
  // Returns a Future<void> the caller can await. `estimated_ns` is the
  // expected duration of the task (used for tuning).
  template <typename Fn>
  Future<void> Execute(ThreadPool& pool, Fn&& work, int64_t estimated_ns,
                       bool cpu_bound, ExecMode requested = ExecMode::kAuto) {
    const ExecMode mode =
        tuner_.Decide(estimated_ns, cpu_bound, Utilization(pool), requested);
    switch (mode) {
      case ExecMode::kThread:
        return Future<void>::FromThread(
            std::function<void()>(std::forward<Fn>(work)), -1);
      case ExecMode::kPinnedThread:
        return Future<void>::FromThread(
            std::function<void()>(std::forward<Fn>(work)), PickCpu());
      case ExecMode::kCoroutine:
      case ExecMode::kAuto:
      default:
        return RunCoroutine(pool, std::forward<Fn>(work));
    }
  }

  // Super-complex CPU task: run on a dedicated pinned thread.
  template <typename Fn>
  Future<void> RunPinned(Fn&& work) {
    return Future<void>::FromThread(std::function<void()>(std::forward<Fn>(work)),
                                    PickCpu());
  }

 private:
  template <typename Fn>
  static Future<void> RunCoroutine(ThreadPool& pool, Fn&& work) {
    return Future<void>::Async(
        [&pool, work = std::forward<Fn>(work)]() -> Task<void> {
          co_await pool.Schedule();
          work();
          co_return;
        },
        pool);
  }

  static double Utilization(const ThreadPool& pool);
  static int PickCpu();

  ComputeTuner tuner_;
};

}  // namespace lark::coro
