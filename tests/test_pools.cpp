// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for the coro pool differentiation, CPU affinity, and compute
// auto-tuning (thread vs coroutine vs pinned thread).

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "coro/coro.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool cond, const char* expr, const char* file, int line) {
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::cerr << "FAIL: " << file << ":" << line << ": " << expr << "\n";
  }
}
#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)

using namespace std::chrono_literals;
using namespace lark::coro;

// ─────────────────────────────────────────────────────────────────────────────
// Differentiated pools
// ─────────────────────────────────────────────────────────────────────────────
void TestPools() {
  std::cout << "Test differentiated pools...\n";

  // Auto-tuned defaults
  Pools pools;
  CHECK(pools.Threads(PoolKind::kCompute) == std::thread::hardware_concurrency());
  CHECK(pools.Threads(PoolKind::kIo) == 2 * std::thread::hardware_concurrency());
  CHECK(pools.Threads(PoolKind::kLowLoad) == 2);
  CHECK(pools.Threads(PoolKind::kDag) == std::thread::hardware_concurrency());
  CHECK(pools.Threads(PoolKind::kDaemon) == 1);

  CHECK(std::string(PoolKindName(PoolKind::kCompute)) == "compute");
  CHECK(std::string(PoolKindName(PoolKind::kDaemon)) == "daemon");

  // Custom configs
  ThreadPoolConfig compute;
  compute.threads = 3;
  ThreadPoolConfig io;
  io.threads = 2;
  Pools custom(compute, io, {}, {}, {});
  CHECK(custom.Threads(PoolKind::kCompute) == 3);
  CHECK(custom.Threads(PoolKind::kIo) == 2);
  CHECK(custom.Threads(PoolKind::kLowLoad) == 2);  // still default

  // affinity config builds without error and sizes correctly
  ThreadPoolConfig aff;
  aff.threads = 2;
  aff.affinity = {0, 1};
  ThreadPool aff_pool(aff);
  CHECK(aff_pool.size() == 2);
  CHECK(aff_pool.config().affinity.size() == 2);

  // pending() / backpressure knob
  ThreadPoolConfig capped;
  capped.threads = 1;
  capped.max_queue = 2;
  ThreadPool cap_pool(capped);
  CHECK(cap_pool.pending() == 0);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeTuner decisions
// ─────────────────────────────────────────────────────────────────────────────
void TestTuner() {
  std::cout << "Test ComputeTuner decisions...\n";

  ComputeTuner tuner;
  // I/O-bound -> always coroutine
  CHECK(tuner.Decide(60 * 1000 * 1000, /*cpu_bound=*/false, 0.0) ==
        ExecMode::kCoroutine);
  // light CPU work, idle pool -> coroutine
  CHECK(tuner.Decide(100 * 1000, true, 0.1) == ExecMode::kCoroutine);
  // heavy CPU work -> dedicated thread
  CHECK(tuner.Decide(10 * 1000 * 1000, true, 0.1) == ExecMode::kThread);
  // super-heavy CPU work -> pinned thread
  CHECK(tuner.Decide(200 * 1000 * 1000, true, 0.1) == ExecMode::kPinnedThread);
  // overloaded pool pushes medium work to a thread
  CHECK(tuner.Decide(100 * 1000, true, 0.95) == ExecMode::kThread);
  // requested mode is honored
  CHECK(tuner.Decide(100 * 1000, true, 0.1, ExecMode::kThread) ==
        ExecMode::kThread);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeExecutor: run with automatic thread-vs-coroutine choice
// ─────────────────────────────────────────────────────────────────────────────
void TestComputeExecutor() {
  std::cout << "Test ComputeExecutor...\n";
  ThreadPool pool(4);

  ComputeExecutor executor;
  std::atomic<int> ran{0};

  // light coroutine task
  auto light = executor.Execute(pool, [&] { ran.fetch_add(1); }, 100 * 1000,
                                /*cpu_bound=*/true);
  light.Wait();
  CHECK(ran.load() == 1);

  // heavy task -> dedicated thread path
  auto heavy = executor.Execute(pool, [&] { ran.fetch_add(10); }, 20 * 1000 * 1000,
                                /*cpu_bound=*/true);
  heavy.Wait();
  CHECK(ran.load() == 11);

  // I/O-bound -> coroutine path
  auto io = executor.Execute(pool, [&] { ran.fetch_add(100); }, 1000 * 1000,
                             /*cpu_bound=*/false);
  io.Wait();
  CHECK(ran.load() == 111);

  // super-heavy -> pinned thread
  auto pinned = executor.RunPinned([&] { ran.fetch_add(1000); });
  pinned.Wait();
  CHECK(ran.load() == 1111);

  // error in a task surfaces as a Future error
  auto bad = executor.Execute(pool, [] { throw std::runtime_error("x"); },
                              100 * 1000, true);
  bool threw = false;
  try {
    bad.Wait();
    bad.Get();
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// CPU affinity / Future::FromThread
// ─────────────────────────────────────────────────────────────────────────────
void TestAffinityAndFromThread() {
  std::cout << "Test CPU affinity + Future::FromThread...\n";

  // PinCurrentThread must be callable without crashing (best-effort per OS).
  const bool pinned = PinCurrentThread(0);
  (void)pinned;

  std::atomic<int> got{0};
  auto f = Future<void>::FromThread([&] { got = 7; }, /*cpu=*/0);
  f.Wait();
  CHECK(got.load() == 7);

  auto bad = Future<void>::FromThread([] { throw std::runtime_error("boom"); });
  bool threw = false;
  try {
    bad.Get();
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== coro Pools / Tuning / CPU Affinity Tests ===\n\n";
  TestPools();
  TestTuner();
  TestComputeExecutor();
  TestAffinityAndFromThread();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
            << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
