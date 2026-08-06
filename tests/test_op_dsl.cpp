// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for declarative dag graph building: OpRegistry class collection
// (LARK_OP), KV form and the DSL form.

#include <iostream>
#include <memory>
#include <string>

#include "coro/coro.h"
#include "dag/op.h"
#include "dag/op_executor.h"
#include "dag/op_graph_builder.h"
#include "dag/op_graph.h"
#include "dag/op_registry.h"

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
// Registered ops (auto-collected into the process-wide OpRegistry via LARK_OP)
// ─────────────────────────────────────────────────────────────────────────────
class DslFetchOp : public lark::dag::Op {
 public:
  const std::string& Name() const noexcept override {
    static const std::string n = "DslFetchOp";
    return n;
  }
  lark::coro::Task<void> Execute(lark::dag::Context& data) override {
    data.Set("x", data.Get<int>("base") + 1);
    co_return;
  }
};
class DslDoubleOp : public lark::dag::Op {
 public:
  const std::string& Name() const noexcept override {
    static const std::string n = "DslDoubleOp";
    return n;
  }
  lark::coro::Task<void> Execute(lark::dag::Context& data) override {
    data.Set("y", data.Get<int>("x") * 2);
    co_return;
  }
};
class DslConstOp : public lark::dag::Op {
 public:
  const std::string& Name() const noexcept override {
    static const std::string n = "DslConstOp";
    return n;
  }
  lark::coro::Task<void> Execute(lark::dag::Context& data) override {
    data.Set("c", 100);
    co_return;
  }
};
class DslSumOp : public lark::dag::Op {
 public:
  const std::string& Name() const noexcept override {
    static const std::string n = "DslSumOp";
    return n;
  }
  lark::coro::Task<void> Execute(lark::dag::Context& data) override {
    data.Set("total", data.Get<int>("y") + data.Get<int>("c"));
    co_return;
  }
};
LARK_OP("DslFetchOp", DslFetchOp);
LARK_OP("DslDoubleOp", DslDoubleOp);
LARK_OP("DslConstOp", DslConstOp);
LARK_OP("DslSumOp", DslSumOp);

// ─────────────────────────────────────────────────────────────────────────────
// Registry / class collection
// ─────────────────────────────────────────────────────────────────────────────
void TestRegistry() {
  std::cout << "Test OpRegistry class collection...\n";

  auto& reg = lark::dag::OpRegistry::Instance();
  CHECK(reg.Contains("DslFetchOp"));
  CHECK(reg.Contains("DslSumOp"));
  CHECK(!reg.Contains("NopeOp"));

  auto op = reg.Create("DslConstOp", "any_id");
  CHECK(op != nullptr);
  CHECK(op->Name() == "DslConstOp");

  bool saw_all = false;
  int found = 0;
  for (const auto& t : reg.RegisteredTypes()) {
    if (t == "DslFetchOp" || t == "DslDoubleOp" || t == "DslConstOp" ||
        t == "DslSumOp")
      ++found;
  }
  CHECK(found == 4);

  CHECK_THROWS([&] { reg.Create("Ghost"); });

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// KV form
// ─────────────────────────────────────────────────────────────────────────────
void TestKvBuild() {
  std::cout << "Test KV graph build (id:Type)...\n";

  lark::dag::OpGraphBuilder builder;

  // selectOp:dataFetchOp style — node id : registered type
  auto graph = builder.Build({
      lark::dag::OpDef{"DslFetchOp", {}, "fetch"},
      lark::dag::OpDef{"DslDoubleOp", {"fetch"}, "dbl"},
      lark::dag::OpDef{"DslFetchOp", {"dbl"}, "fetch2"},  // same type, new id
  });
  CHECK(graph->size() == 3);
  graph->Compile();
  CHECK(graph->ops().size() == 3);

  lark::coro::Pools pools;
  lark::dag::OpExecutor executor(pools.Get(lark::coro::PoolKind::kDag));
  lark::dag::Context data;
  data.Set("base", 10);
  executor.Execute(*graph, data);

  // fetch sets x=11, dbl sets y=22, fetch2 re-reads base and sets x=11
  CHECK(data.Get<int>("x") == 11);
  CHECK(data.Get<int>("y") == 22);
  CHECK(executor.AllSucceeded());

  // unknown type / unknown dep
  CHECK_THROWS([&] {
    builder.Build({lark::dag::OpDef{"GhostOp"}});
  });
  CHECK_THROWS([&] {
    builder.Build({lark::dag::OpDef{"DslFetchOp", {"nope"}}});
  });

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// DSL form
// ─────────────────────────────────────────────────────────────────────────────
void TestDslBuild() {
  std::cout << "Test DSL graph build...\n";

  lark::dag::OpGraphBuilder builder;

  // linear chain + fan-in (rank reuses id "total")
  auto graph = builder.BuildDsl(
      "fetch:DslFetchOp -> dbl:DslDoubleOp -> total:DslSumOp\n"
      "c:DslConstOp -> total:DslSumOp\n");
  CHECK(graph->size() == 4);
  graph->Compile();

  lark::coro::Pools pools;
  lark::dag::OpExecutor executor(pools.Get(lark::coro::PoolKind::kDag));
  lark::dag::Context data;
  data.Set("base", 10);
  executor.Execute(*graph, data);

  // fetch -> x=11; dbl -> y=22; c -> c=100; total = y + c = 122
  CHECK(data.Get<int>("total") == 122);
  CHECK(data.Get<int>("c") == 100);
  CHECK(executor.AllSucceeded());

  // bare names (id == type)
  auto bare = builder.BuildDsl("DslConstOp -> DslSumOp");
  CHECK(bare->size() == 2);

  // errors: same id with two types, and an unknown type
  CHECK_THROWS([&] {
    builder.BuildDsl("a:DslConstOp -> a:DslDoubleOp");
  });
  CHECK_THROWS([&] { builder.BuildDsl("x:NoSuchOp"); });

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== dag DSL / KV / OpRegistry Tests ===\n\n";
  TestRegistry();
  TestKvBuild();
  TestDslBuild();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
            << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
