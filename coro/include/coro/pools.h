// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Differentiated worker pools.
//
// The framework owns one thread pool per purpose, so coroutine work is
// scheduled on the right class of worker instead of a single shared pool:
//   compute / io / lowload / dag / daemon.
// Sizes are auto-tuned per kind by default and overridable via config; each
// pool can pin its workers to specific CPUs and carry backpressure knobs.

#include <array>
#include <cstddef>
#include <memory>

#include "coro/thread_pool.h"

namespace lark::coro {

constexpr std::size_t kNumPoolKinds = 5;  // PoolKind count

class Pools {
 public:
  // Create all five pools. Pass a config per kind (empty = auto defaults).
  Pools(ThreadPoolConfig compute = {}, ThreadPoolConfig io = {},
        ThreadPoolConfig lowload = {}, ThreadPoolConfig dag = {},
        ThreadPoolConfig daemon = {});

  Pools(const Pools&) = delete;
  Pools& operator=(const Pools&) = delete;

  ThreadPool& Get(PoolKind kind) { return *pools_[IndexOf(kind)]; }
  const ThreadPool& Get(PoolKind kind) const { return *pools_[IndexOf(kind)]; }
  std::size_t Threads(PoolKind kind) const { return Get(kind).size(); }

 private:
  static std::size_t IndexOf(PoolKind kind) {
    return static_cast<std::size_t>(kind);
  }

  std::array<std::unique_ptr<ThreadPool>, kNumPoolKinds> pools_;
};

}  // namespace lark::coro
