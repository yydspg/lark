// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for the business-oriented dag Op layer: aspect-based condition skip,
// coroutine execution on the dag pool, and OOP business inheritance.

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "dag/op.h"
#include "dag/op_executor.h"
#include "dag/op_graph.h"
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

void ExpectThrow(std::function<void()> fn, const char* what, const char* file,
                 int line) {
  ++g_checks;
  bool threw = false;
  try {
    fn();
  } catch (const std::exception&) {
    threw = true;
  }
  if (!threw) {
    ++g_failures;
    std::cerr << "FAIL: " << file << ":" << line << ": expected throw from "
              << what << "\n";
  }
}
#define CHECK_THROWS(fn) ExpectThrow((fn), #fn, __FILE__, __LINE__)

// ─────────────────────────────────────────────────────────────────────────────
// Business ops (OOP: only Name() + Execute() are overridden)
// ─────────────────────────────────────────────────────────────────────────────
class FetchOp : public lark::dag::Op {
 public:
  const std::string& Name() const noexcept override {
    static const std::string n = "fetch";
    return n;
  }
  lark::coro::Task<void> Execute(lark::dag::Context& data) override {
    const int id = data.Get<int>("user_id");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    data.Set("user", std::string("user_") + std::to_string(id));
    co_return;
  }
};

class EnrichOp : public lark::dag::Op {
 public:
  const std::string& Name() const noexcept override {
    static const std::string n = "enrich";
    return n;
  }
  lark::coro::Task<void> Execute(lark::dag::Context& data) override {
    const std::string& user = data.Get<std::string>("user");
    data.Set("enriched", user + "_enriched");
    co_return;
  }
};

class ScoreOp : public lark::dag::Op {
 public:
  const std::string& Name() const noexcept override {
    static const std::string n = "score";
    return n;
  }
  lark::coro::Task<void> Execute(lark::dag::Context& data) override {
    data.Set("score", 99);
    co_return;
  }
};

// Business can also use AddSync-style ops through coro::Pipeline? Not needed.

// Aspect: skip ops whose name is in the "skip" set written into the context.
class SkipSetAspect : public lark::dag::OpAspect {
 public:
  bool ShouldSkip(const lark::dag::Op& op,
                  const lark::dag::Context& data) override {
    if (!data.Has<std::string>("skip")) return false;
    return data.Get<std::string>("skip") == op.Name();
  }
};

// Aspect that counts hooks.
class CountingAspect : public lark::dag::OpAspect {
 public:
  std::atomic<int> before{0};
  std::atomic<int> after{0};
  bool ShouldSkip(const lark::dag::Op&, const lark::dag::Context&) override {
    return false;
  }
  void OnBefore(const lark::dag::Op&, lark::dag::Context&) override {
    before.fetch_add(1);
  }
  void OnAfter(const lark::dag::Op&, lark::dag::Context&,
               lark::dag::OpStatus) override {
    after.fetch_add(1);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────
void TestOpGraphExecution() {
  std::cout << "Test Op graph execution...\n";

  lark::coro::Pools pools;
  auto stats = std::make_shared<lark::metric::StatsMonitor>();
  lark::dag::OpExecutor executor(pools.Get(lark::coro::PoolKind::kDag), stats);

  auto counter = std::make_shared<CountingAspect>();
  lark::dag::OpGraph graph;
  graph.AddOp(std::make_shared<FetchOp>());
  graph.AddOp(std::make_shared<EnrichOp>());
  graph.AddOp(std::make_shared<ScoreOp>());
  graph.DependsOn("fetch", "enrich");
  graph.DependsOn("enrich", "score");
  graph.AddAspect(counter);

  lark::dag::Context data;
  data.Set("user_id", 7);
  executor.Execute(graph, data);

  CHECK(data.Get<std::string>("enriched") == "user_7_enriched");
  CHECK(data.Get<int>("score") == 99);
  CHECK(counter->before.load() == 3);
  CHECK(counter->after.load() == 3);
  CHECK(executor.AllSucceeded());
  CHECK(executor.Status(graph.ops()[0]) == lark::dag::OpStatus::kSuccess);
  CHECK(stats->event_count() >= 3);  // op.start / op.end events

  std::cout << "  done\n";
}

void TestAspectSkip() {
  std::cout << "Test aspect-based condition skip...\n";

  lark::coro::Pools pools;
  lark::dag::OpExecutor executor(pools.Get(lark::coro::PoolKind::kDag));

  lark::dag::OpGraph graph;
  graph.AddOp(std::make_shared<FetchOp>());
  graph.AddOp(std::make_shared<EnrichOp>());
  graph.AddOp(std::make_shared<ScoreOp>());
  graph.DependsOn("fetch", "enrich");
  graph.DependsOn("enrich", "score");
  graph.AddAspect(std::make_shared<SkipSetAspect>());

  // skip the "score" op conditionally
  lark::dag::Context data;
  data.Set("user_id", 3);
  data.Set("skip", std::string("score"));
  executor.Execute(graph, data);

  CHECK(executor.Status(graph.ops()[0]) == lark::dag::OpStatus::kSuccess);  // fetch
  CHECK(executor.Status(graph.ops()[1]) == lark::dag::OpStatus::kSuccess);  // enrich
  CHECK(executor.Status(graph.ops()[2]) == lark::dag::OpStatus::kSkipped);  // score
  // score was skipped: its field was never written
  CHECK(!data.Has<int>("score"));
  // graph still drains
  CHECK(data.Get<std::string>("enriched") == "user_3_enriched");
  CHECK(executor.AllSucceeded());  // skipped counts as ok

  std::cout << "  done\n";
}

void TestOpErrorDoesNotPoison() {
  std::cout << "Test op failure does not poison dependents...\n";

  class BoomOp : public lark::dag::Op {
   public:
    const std::string& Name() const noexcept override {
      static const std::string n = "boom";
      return n;
    }
    lark::coro::Task<void> Execute(lark::dag::Context&) override {
      throw std::runtime_error("kaboom");
      co_return;
    }
  };

  lark::coro::Pools pools;
  lark::dag::OpExecutor executor(pools.Get(lark::coro::PoolKind::kDag));

  lark::dag::OpGraph graph;
  graph.AddOp(std::make_shared<BoomOp>());
  graph.AddOp(std::make_shared<ScoreOp>());
  graph.DependsOn("boom", "score");  // score depends on a failing op

  lark::dag::Context data;
  executor.Execute(graph, data);

  CHECK(executor.Status(graph.ops()[0]) == lark::dag::OpStatus::kFailed);
  CHECK(executor.Status(graph.ops()[1]) == lark::dag::OpStatus::kSuccess);
  CHECK(!executor.AllSucceeded());
  CHECK(data.Get<int>("score") == 99);  // dependent still ran

  std::cout << "  done\n";
}

void TestOpGraphValidation() {
  std::cout << "Test Op graph validation...\n";

  lark::dag::OpGraph g;
  g.AddOp(std::make_shared<FetchOp>());

  // duplicate name
  CHECK_THROWS([&] { g.AddOp(std::make_shared<FetchOp>()); });
  // unknown dependency
  CHECK_THROWS([&] { g.DependsOn("ghost", "fetch"); });

  // cycle
  lark::dag::OpGraph cyc;
  cyc.AddOp(std::make_shared<FetchOp>());
  cyc.AddOp(std::make_shared<EnrichOp>());
  cyc.DependsOn("fetch", "enrich");
  cyc.DependsOn("enrich", "fetch");
  CHECK_THROWS([&] { cyc.Compile(); });

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== dag Op / Aspect Tests (OOP + coro) ===\n\n";
  TestOpGraphExecution();
  TestAspectSkip();
  TestOpErrorDoesNotPoison();
  TestOpGraphValidation();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
            << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
