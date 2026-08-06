// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for the coro orchestration layer: CompletableFuture-style Future<T>
// (Then / ThenAsync / OnError / WhenComplete / AllOf / AnyOf / co_await) and
// the lock-free Context + Pipeline orchestration.

#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "coro/coro.h"
#include "metric/metric.h"

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

void WaitEvent(lark::coro::AsyncEvent& e) {
  while (!e.IsSet()) std::this_thread::sleep_for(1ms);
}

bool Throws(std::function<void()> fn) {
  try {
    fn();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Future basics
// ─────────────────────────────────────────────────────────────────────────────
void TestFutureBasics() {
  std::cout << "Test Future basics...\n";
  lark::coro::ThreadPool pool(4);

  auto done = lark::coro::Future<int>::Just(42);
  CHECK(done.valid());
  CHECK(done.Get() == 42);

  auto err = lark::coro::Future<int>::Error(
      std::make_exception_ptr(std::runtime_error("boom")));
  CHECK(Throws([&] { (void)err.Get(); }));

  auto async = lark::coro::Future<int>::Async(
      [&pool]() -> lark::coro::Task<int> {
        co_await pool.Schedule();
        co_return 7;
      },
      pool);
  CHECK(async.WaitFor(1s));
  CHECK(async.Get() == 7);

  // timeout
  auto slow = lark::coro::Future<int>::Async(
      [&pool]() -> lark::coro::Task<int> {
        co_await pool.Schedule();
        std::this_thread::sleep_for(300ms);
        co_return 1;
      },
      pool);
  CHECK(Throws([&] { (void)slow.Get(10ms); }));
  CHECK(slow.Get() == 1);  // still resolves eventually

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Combinators
// ─────────────────────────────────────────────────────────────────────────────
void TestCombinators() {
  std::cout << "Test Future combinators...\n";
  lark::coro::ThreadPool pool(4);

  // Then (sync transform)
  auto f = lark::coro::Future<int>::Just(21).Then([](int v) { return v * 2; });
  CHECK(f.Get() == 42);

  // Then with a void-returning fn
  int side = 0;
  auto fv = lark::coro::Future<int>::Just(1).Then([&](int) { side = 5; });
  CHECK(fv.valid());
  fv.Wait();
  CHECK(side == 5);

  // error propagates through Then
  auto bad = lark::coro::Future<int>::Error(
      std::make_exception_ptr(std::runtime_error("x")));
  CHECK(Throws([&] { (void)bad.Then([](int v) { return v; }).Get(); }));

  // ThenAsync (compose)
  auto composed = lark::coro::Future<int>::Just(2)
      .ThenAsync([&pool](int v) -> lark::coro::Task<int> {
        co_await pool.Schedule();
        co_return v + 3;
      })
      .ThenAsync([&pool](int v) -> lark::coro::Future<int> {
        return lark::coro::Future<int>::Async(
            [&pool, v]() -> lark::coro::Task<int> {
              co_await pool.Schedule();
              co_return v * 10;
            },
            pool);
      });
  CHECK(composed.Get() == 50);

  // OnError recovery
  auto rec = lark::coro::Future<int>::Error(
      std::make_exception_ptr(std::runtime_error("x")))
      .OnError([](std::exception_ptr) { return 99; });
  CHECK(rec.Get() == 99);

  // WhenComplete observes success and failure
  int seen_ok = -1;
  auto wc = lark::coro::Future<int>::Just(5).WhenComplete(
      [&](const std::optional<int>& v, std::exception_ptr) {
        if (v) seen_ok = *v;
      });
  CHECK(wc.Get() == 5 && seen_ok == 5);

  int seen_err = 0;
  auto wce = lark::coro::Future<int>::Error(
      std::make_exception_ptr(std::runtime_error("x")))
      .WhenComplete([&](const std::optional<int>&, std::exception_ptr e) {
        seen_err = e ? 1 : 0;
      });
  CHECK(Throws([&] { (void)wce.Get(); }));
  CHECK(seen_err == 1);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// AllOf / AnyOf
// ─────────────────────────────────────────────────────────────────────────────
void TestAggregates() {
  std::cout << "Test AllOf / AnyOf...\n";
  lark::coro::ThreadPool pool(4);

  auto mk = [&](int v) {
    return lark::coro::Future<int>::Async(
        [&pool, v]() -> lark::coro::Task<int> {
          co_await pool.Schedule();
          co_return v;
        },
        pool);
  };
  auto a = mk(1);
  auto b = mk(2);
  auto c = mk(3);

  auto all = lark::coro::Future<int>::AllOf({a, b, c});
  auto vals = all.Get();
  CHECK(vals.size() == 3 && vals[0] == 1 && vals[1] == 2 && vals[2] == 3);

  auto any = lark::coro::Future<int>::AnyOf({a, b, c});
  auto [idx, v] = any.Get();
  CHECK(idx < 3 && v >= 1 && v <= 3);

  // AllOf propagates the first error
  auto bad = lark::coro::Future<int>::Error(
      std::make_exception_ptr(std::runtime_error("x")));
  auto allBad = lark::coro::Future<int>::AllOf({a, bad, c});
  CHECK(Throws([&] { (void)allBad.Get(); }));

  // Future<void> aggregates
  auto ua = lark::coro::Future<void>::Async(
      [&pool]() -> lark::coro::Task<void> {
        co_await pool.Schedule();
        co_return;
      },
      pool);
  auto ub = lark::coro::Future<void>::Just();
  auto allVoid = lark::coro::Future<void>::AllOf({ua, ub});
  allVoid.Wait();
  CHECK(allVoid.valid());

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// co_await integration
// ─────────────────────────────────────────────────────────────────────────────
void TestCoAwait() {
  std::cout << "Test co_await Future...\n";
  lark::coro::ThreadPool pool(4);

  auto fut = lark::coro::Future<int>::Async(
      [&pool]() -> lark::coro::Task<int> {
        co_await pool.Schedule();
        co_return 7;
      },
      pool);

  lark::coro::AsyncEvent done;
  int got = -1;
  [&]() -> lark::coro::FireAndForget {
    co_await pool.Schedule();
    got = co_await fut;  // Future<int> is awaitable
    done.Set();
    co_return;
  }();
  WaitEvent(done);
  CHECK(got == 7);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Context + Pipeline
// ─────────────────────────────────────────────────────────────────────────────
void TestContextAndPipeline() {
  std::cout << "Test Context + Pipeline...\n";
  lark::coro::ThreadPool pool(4);
  auto stats = std::make_shared<lark::metric::StatsMonitor>();

  lark::coro::Pipeline pipe(pool, stats);
  pipe.Add("fetch", [&pool](lark::coro::Context& ctx) -> lark::coro::Task<void> {
    co_await pool.Schedule();
    ctx.Set("a", 21);
    co_return;
  });
  pipe.AddSync("double", [](lark::coro::Context& ctx) {
    const int a = ctx.Get<int>("a");
    ctx.Set("b", a * 2);  // 42
  });
  pipe.AddParallel({
      {"p1", [](lark::coro::Context& ctx) -> lark::coro::Task<void> {
         ctx.Set("c", 10);
         co_return;
       }},
      {"p2", [](lark::coro::Context& ctx) -> lark::coro::Task<void> {
         ctx.Set("d", 32);
         co_return;
       }},
  });
  pipe.AddSync("sum", [](lark::coro::Context& ctx) {
    ctx.Set("total",
            ctx.Get<int>("b") + ctx.Get<int>("c") + ctx.Get<int>("d"));
  });

  auto fut = pipe.Run();
  fut.Wait();
  CHECK(pipe.context().Get<int>("total") == 42 + 10 + 32);
  CHECK(pipe.context().Has<int>("a"));
  CHECK(stats->event_count() >= 5);  // pipeline.step events
  CHECK(stats->error_count() == 0);

  // A failing step surfaces as a Future error
  lark::coro::Pipeline bad(pool);
  bad.AddSync("boom", [](lark::coro::Context&) {
    throw std::runtime_error("kaboom");
  });
  auto bf = bad.Run();
  bool threw = false;
  try {
    bf.Get();
  } catch (const std::exception& e) {
    threw = true;
    CHECK(std::string(e.what()) == "kaboom");
  }
  CHECK(threw);

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== coro Orchestration (Future / Context / Pipeline) Tests ===\n\n";
  TestFutureBasics();
  TestCombinators();
  TestAggregates();
  TestCoAwait();
  TestContextAndPipeline();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
            << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
