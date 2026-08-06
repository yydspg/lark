// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "coro/tuner.h"

#include <algorithm>

namespace lark::coro {

ExecMode ComputeTuner::Decide(int64_t estimated_ns, bool cpu_bound,
                              double pool_utilization,
                              ExecMode requested) const {
  if (requested != ExecMode::kAuto) return requested;

  // I/O-bound work always runs cooperatively on a pool — it suspends on I/O
  // and releases its worker.
  if (!cpu_bound) return ExecMode::kCoroutine;

  // CPU-bound: heavier work is moved off the cooperative pool.
  if (estimated_ns >= options_.pinned_above_ns) return ExecMode::kPinnedThread;
  if (estimated_ns >= options_.thread_above_ns ||
      pool_utilization > options_.max_pool_utilization) {
    return ExecMode::kThread;
  }
  return ExecMode::kCoroutine;
}

double ComputeExecutor::Utilization(const ThreadPool& pool) {
  const std::size_t threads = pool.size();
  if (threads == 0) return 0.0;
  return std::min(1.0, static_cast<double>(pool.pending()) /
                           static_cast<double>(threads));
}

int ComputeExecutor::PickCpu() {
  static std::atomic<unsigned> counter{0};
  const unsigned cpus = std::max(1u, std::thread::hardware_concurrency());
  return static_cast<int>(counter.fetch_add(1) % cpus);
}

}  // namespace lark::coro
