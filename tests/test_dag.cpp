// Self-contained tests for the DAG framework (no external test dependency).
// Exits non-zero if any check fails; wired into CTest.

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "dag/dag.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const char* expr, const char* file, int line) {
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::cerr << "FAIL: " << file << ":" << line << ": " << expr << "\n";
  }
}

template <typename Fn>
void ExpectThrow(Fn&& fn, const char* what, const char* file, int line) {
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

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)
#define CHECK_THROWS(fn) ExpectThrow((fn), #fn, __FILE__, __LINE__)
#define CHECK_NOTHROW(fn) Check((fn, true), #fn, __FILE__, __LINE__)

using namespace std::chrono_literals;

// ---- test domains / nodes ------------------------------------------------
struct Bag {
  std::atomic<int> counter{0};
};

class IncNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    lark::RequireDomain<Bag>(ctx).counter.fetch_add(1, std::memory_order_relaxed);
  }
};

class SleepNode : public lark::Node {
  void Compute(lark::IContext&) override { std::this_thread::sleep_for(100ms); }
};

class SourceNode : public lark::Node {
  void Compute(lark::IContext& ctx) override { lark::Set(ctx, "x", 7); }
};

class SinkNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    if (lark::Has<int>(ctx, "x")) {
      const auto& x = lark::Get<int>(ctx, "x");
      if (x == 7) {
        lark::Set(ctx, "y", 2 * x);  // proves this ran after SourceNode
      }
    }
  }
};

class BoomNode : public lark::Node {
  void Compute(lark::IContext&) override {
    throw std::runtime_error("boom");
  }
  bool Fallback(lark::IContext& ctx, std::exception_ptr) override {
    lark::Set(ctx, "recovered", 1);
    return true;
  }
};

class BoomNoFallback : public lark::Node {
  void Compute(lark::IContext&) override {
    throw std::runtime_error("boom");
  }
};

// Node that demonstrates pool hopping via Schedule: starts on the IO pool
// (the default for all nodes), hops to the compute pool for a CPU-bound step,
// then hops back to the IO pool. Records the thread id at each step so the
// test can verify the hops actually happened.
class PoolHopNode : public lark::Node {
  lark::Task<void> Run(lark::IContext& ctx) override {
    auto io_tid = std::this_thread::get_id();
    lark::Set(ctx, "io_tid", io_tid);

    // Hop to the compute pool.
    co_await lark::Schedule(ctx, lark::PoolKind::kCompute);
    auto compute_tid = std::this_thread::get_id();
    lark::Set(ctx, "compute_tid", compute_tid);

    // Hop back to the IO pool.
    co_await lark::Schedule(ctx, lark::PoolKind::kIo);
    auto io2_tid = std::this_thread::get_id();
    lark::Set(ctx, "io2_tid", io2_tid);
    co_return;
  }
};

void RegisterAll(lark::NodeRegistry& reg) {
  reg.Register("inc", [] { return std::make_unique<IncNode>(); });
  reg.Register("sleep", [] { return std::make_unique<SleepNode>(); });
  reg.Register("source", [] { return std::make_unique<SourceNode>(); });
  reg.Register("sink", [] { return std::make_unique<SinkNode>(); });
  reg.Register("boom", [] { return std::make_unique<BoomNode>(); });
  reg.Register("boom_nf", [] { return std::make_unique<BoomNoFallback>(); });
  reg.Register("pool_hop", [] { return std::make_unique<PoolHopNode>(); });
}

// ---- tests ---------------------------------------------------------------
void TestContext() {
  lark::DefaultContext ctx;
  lark::Set(ctx, "n", 42);
  CHECK(lark::Has<int>(ctx, "n"));
  CHECK(!lark::Has<std::string>(ctx, "n"));       // type mismatch
  CHECK(lark::Get<int>(ctx, "n") == 42);
  CHECK_THROWS([&] { lark::Get<double>(ctx, "n"); });      // wrong type -> throws
  CHECK_THROWS([&] { lark::Get<int>(ctx, "missing"); });   // missing key -> throws

  lark::ProvideDomain<Bag>(ctx);
  CHECK_NOTHROW(lark::Domain<Bag>(ctx));
  lark::RequireDomain<Bag>(ctx).counter = 5;
  CHECK(lark::Domain<Bag>(ctx).counter.load() == 5);

  ctx.Erase("n");
  CHECK(!lark::Has<int>(ctx, "n"));
}

void TestBuilderValidation() {
  lark::NodeRegistry reg;
  RegisterAll(reg);
  lark::GraphBuilder builder(reg);

  CHECK_THROWS([&] { builder.Build({{"ghost"}}); });          // unknown type
  CHECK_THROWS([&] { builder.Build({{"inc"}, {"inc"}}); });   // duplicate id
  CHECK_THROWS([&] { builder.Build({{"inc", {"nope"}}}); });  // missing dep
  CHECK_THROWS([&] {                                          // cycle
    builder.Build({{"inc", {"b"}, "a"}, {"inc", {"a"}, "b"}});
  });

  // A valid graph builds fine.
  auto graph = builder.Build({{"source"}, {"sink", {"source"}}});
  CHECK(graph->size() == 2);
  CHECK(graph->Find("sink") != nullptr);
  CHECK(graph->Find("sink")->dependencies().size() == 1);
}

void TestDiamondExecution() {
  lark::NodeRegistry reg;
  RegisterAll(reg);
  lark::GraphBuilder builder(reg);

  auto graph = builder.Build({
      {"inc", {}, "a"},
      {"inc", {"a"}, "b"},
      {"inc", {"a"}, "c"},
      {"inc", {"b", "c"}, "d"},
  });

  lark::DefaultContext ctx;
  lark::ProvideDomain<Bag>(ctx);
  lark::Executor executor(4, 4);
  executor.Execute(*graph, ctx);

  CHECK(lark::Domain<Bag>(ctx).counter.load() == 4);
  for (const auto& node : graph->nodes()) {
    CHECK(node->status() == lark::NodeStatus::kSuccess);
  }
}

void TestOrderingAndDataFlow() {
  lark::NodeRegistry reg;
  RegisterAll(reg);
  lark::GraphBuilder builder(reg);

  auto graph = builder.Build({{"source"}, {"sink", {"source"}}});
  lark::DefaultContext ctx;
  lark::Executor executor(2, 2);
  executor.Execute(*graph, ctx);

  // sink only sets y when it observed source's x -> proves ordering.
  CHECK(lark::Has<int>(ctx, "y"));
  CHECK(lark::Get<int>(ctx, "y") == 14);
}

void TestParallelInputs() {
  lark::NodeRegistry reg;
  RegisterAll(reg);
  lark::GraphBuilder builder(reg);

  // Four independent 100ms nodes; with >=4 workers they run in parallel.
  auto graph = builder.Build({
      {"sleep", {}, "s1"},
      {"sleep", {}, "s2"},
      {"sleep", {}, "s3"},
      {"sleep", {}, "s4"},
  });

  lark::DefaultContext ctx;
  lark::Executor executor(4, 4);
  const auto start = std::chrono::steady_clock::now();
  executor.Execute(*graph, ctx);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  // Sequential would be ~400ms; parallel should be well under 300ms.
  CHECK(ms < 300);
}

void TestFallback() {
  lark::NodeRegistry reg;
  RegisterAll(reg);
  lark::GraphBuilder builder(reg);

  auto graph = builder.Build({{"boom"}, {"boom_nf"}, {"sink", {"boom"}}});
  lark::DefaultContext ctx;
  lark::Executor executor(2, 2);
  executor.Execute(*graph, ctx);

  CHECK(graph->Find("boom")->status() == lark::NodeStatus::kFallback);
  CHECK(graph->Find("boom_nf")->status() == lark::NodeStatus::kFailed);
  CHECK(graph->Find("boom_nf")->error() != nullptr);
  CHECK(lark::Has<int>(ctx, "recovered"));
  // Downstream still runs after a failed dependency (graph completes).
  CHECK(graph->Find("sink")->status() == lark::NodeStatus::kSuccess);
}

void TestGraphReuse() {
  lark::NodeRegistry reg;
  RegisterAll(reg);
  lark::GraphBuilder builder(reg);

  auto graph = builder.Build({{"inc", {}, "a"}, {"inc", {"a"}, "b"}});
  lark::Executor executor(2, 2);

  lark::DefaultContext ctx1;
  lark::ProvideDomain<Bag>(ctx1);
  executor.Execute(*graph, ctx1);
  CHECK(lark::Domain<Bag>(ctx1).counter.load() == 2);

  // Re-run the same graph with a fresh context; run state is reset internally.
  lark::DefaultContext ctx2;
  lark::ProvideDomain<Bag>(ctx2);
  executor.Execute(*graph, ctx2);
  CHECK(lark::Domain<Bag>(ctx2).counter.load() == 2);
}

void TestWideGraphStress() {
  lark::NodeRegistry reg;
  RegisterAll(reg);
  lark::GraphBuilder builder(reg);

  // 500 independent nodes on a small (2-thread) pool: must not deadlock and
  // must all complete (coroutine suspension keeps the pool from starving).
  std::vector<lark::NodeDef> defs;
  for (int i = 0; i < 500; ++i) {
    defs.push_back({"inc", {}, "n" + std::to_string(i)});
  }
  auto graph = builder.Build(defs);

  lark::DefaultContext ctx;
  lark::ProvideDomain<Bag>(ctx);
  lark::Executor executor(2, 2);
  executor.Execute(*graph, ctx);
  CHECK(lark::Domain<Bag>(ctx).counter.load() == 500);
}

// ---- Custom IContext implementation --------------------------------------
// Demonstrates that business code can plug in any storage strategy.
class CountingContext : public lark::IContext {
 public:
  void SetVoid(const lark::string& key, lark::shared_ptr<void> value,
               lark::type_index type) override {
    ++writes_;
    data_.insert_or_assign(key, Entry{type, std::move(value)});
  }
  lark::shared_ptr<const void> GetVoid(const lark::string& key,
                                      lark::type_index type) const override {
    ++reads_;
    auto it = data_.find(key);
    if (it == data_.end() || it->second.type != type) return nullptr;
    return it->second.value;
  }
  bool Has(const lark::string& key, lark::type_index type) const override {
    auto it = data_.find(key);
    return it != data_.end() && it->second.type == type;
  }
  void Erase(const lark::string& key) override { data_.erase(key); }

  void SetDomainVoid(lark::type_index type,
                     lark::shared_ptr<void> value) override {
    domains_.insert_or_assign(type, std::move(value));
  }
  lark::shared_ptr<const void> GetDomainVoid(lark::type_index type) const override {
    auto it = domains_.find(type);
    return it == domains_.end() ? nullptr : it->second;
  }

  lark::ThreadPool& GetPool(lark::PoolKind kind) override {
    switch (kind) {
      case lark::PoolKind::kCompute:
        return compute_;
      case lark::PoolKind::kIo:
        return io_;
      case lark::PoolKind::kBackground:
        return background_;
    }
    return io_;  // unreachable
  }

  int reads() const { return reads_; }
  int writes() const { return writes_; }

 private:
  struct Entry {
    lark::type_index type;
    lark::shared_ptr<void> value;
  };
  std::unordered_map<lark::string, Entry> data_;
  std::unordered_map<lark::type_index, lark::shared_ptr<void>> domains_;
  mutable int reads_ = 0;
  int writes_ = 0;
  lark::ThreadPool compute_{2};
  lark::ThreadPool io_{2};
  lark::ThreadPool background_{2};
};

void TestCustomContext() {
  lark::NodeRegistry reg;
  RegisterAll(reg);
  lark::GraphBuilder builder(reg);

  auto graph = builder.Build({{"source"}, {"sink", {"source"}}});
  CountingContext ctx;
  lark::Executor executor(2, 2);
  executor.Execute(*graph, ctx);

  // source writes x; sink reads x, writes y.
  CHECK(ctx.writes() >= 2);
  CHECK(ctx.reads() >= 1);
  CHECK(lark::Has<int>(ctx, "y"));
  CHECK(lark::Get<int>(ctx, "y") == 14);
}

// Verifies that:
//   * Executor installs both pools into DefaultContext (reachable via GetPool).
//   * Schedule(ctx, PoolKind) hops the coroutine onto the chosen pool.
//   * compute_worker_count / io_worker_count reflect the configured sizes.
void TestPoolSelectionAndSchedule() {
  lark::NodeRegistry reg;
  RegisterAll(reg);
  lark::GraphBuilder builder(reg);

  auto graph = builder.Build({{"pool_hop"}});

  lark::DefaultContext ctx;
  lark::Executor executor(3, 5);
  CHECK(executor.compute_worker_count() == 3);
  CHECK(executor.io_worker_count() == 5);

  executor.Execute(*graph, ctx);

  CHECK(lark::Has<std::thread::id>(ctx, "io_tid"));
  CHECK(lark::Has<std::thread::id>(ctx, "compute_tid"));
  CHECK(lark::Has<std::thread::id>(ctx, "io2_tid"));
  const auto& io_tid = lark::Get<std::thread::id>(ctx, "io_tid");
  const auto& compute_tid = lark::Get<std::thread::id>(ctx, "compute_tid");
  // The compute step must have run on a different thread than the IO step
  // (the pools are disjoint: 3 compute workers, 5 IO workers).
  CHECK(compute_tid != io_tid);
  // After hopping back to the IO pool, we're on some IO worker (not
  // necessarily the same one we started on, but always an IO worker).
}

}  // namespace

int main() {
  TestContext();
  TestBuilderValidation();
  TestDiamondExecution();
  TestOrderingAndDataFlow();
  TestParallelInputs();
  TestFallback();
  TestGraphReuse();
  TestWideGraphStress();
  TestCustomContext();
  TestPoolSelectionAndSchedule();

  std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
