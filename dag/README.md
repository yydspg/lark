# lark_dag — DAG Business Execution Framework

A high-performance, fully-asynchronous DAG framework for business node
orchestration. One detached coroutine per node; nodes await their
dependencies' completion events on a coroutine pool, so independent nodes run
concurrently and a small pool never deadlocks on a wide graph.

## Highlights

- **Node / Graph / GraphBuilder / NodeRegistry** — materialize and validate a
  DAG (`dag/dag.h` umbrella header; `LARK_NODE` auto-registration).
- **Executor** — coroutine scheduling across three pools
  (compute / IO / background), with fallback (graceful degradation) and batch-disable.
- **Per-node timing** — every `Node` records `elapsed()` and `started_at()`;
  attach a `StatsCollector` for a waterfall summary.
- **Batch-disable** — `Execute(graph, ctx, {"a", "b"})` skips nodes for a run
  (`NodeStatus::kSkipped`), completion still fires, the graph always drains.
- **Unified monitoring** — the executor emits `metric` `"dag"` events
  (`node.start/success/failure/fallback/skipped`); attach any
  `metric::Monitor` implementation.

## Quick start

```cpp
#include "dag/dag.h"

// Registered node types (LARK_NODE("fetch_user", FetchUserNode), ...).
lark::GraphBuilder builder;
auto graph = builder.Build({
    {"fetch_user", {}, "fetch_user"},
    {"rank", {"fetch_user"}, "rank"},
});

lark::DefaultContext ctx;
lark::Executor executor;
executor.Execute(*graph, ctx);            // blocking; async underneath
```

### Monitoring + timing

```cpp
lark::StatsCollector stats;
executor.SetMonitor(std::make_shared<lark::StatsCollector>(stats));
executor.Execute(*graph, ctx);
std::cout << stats.stats().summary();     // per-node elapsed / started_at
```

### Batch-disable

```cpp
executor.Execute(*graph, ctx, {"rank"});  // skip "rank" this run (kSkipped)
```

## Op layer — business-oriented + coroutine (recommended for new code)

A cleaner, OOP-friendly layer on top of the same DAG ideas. Business subclasses
`dag::Op` and writes **only business logic** (no framework details):

```cpp
#include "dag/op.h"
#include "dag/op_executor.h"
#include "dag/op_graph.h"

class FetchUser : public lark::dag::Op {
 public:
  const std::string& Name() const noexcept override {
    static const std::string n = "fetch_user"; return n;
  }
  lark::coro::Task<void> Execute(lark::dag::Context& data) override {
    data.Set("user", co_await LoadUser(data.Get<int>("user_id")));
  }
};

class SkipExpensive : public lark::dag::OpAspect {          // (aspect)
 public:
  bool ShouldSkip(const lark::dag::Op& op, const lark::dag::Context& data) override {
    return data.Has<bool>("cheap_mode") && data.Get<bool>("cheap_mode");
  }
};
```

- **`dag::Op`** — business base: override `Name()` + `Execute(Context&)` only.
  Data flows through the lock-free `coro::Context` (no locks by convention).
- **`dag::OpAspect` (aspect)** — cross-cutting hooks around every op:
  `ShouldSkip` (condition-based **auto-skip**, unlike op-internal skip logic),
  `OnBefore`, `OnAfter`. Apply tracing / flags / feature switches uniformly.
- **`dag::OpGraph`** — `AddOp` / `DependsOn` / `AddAspect`; validates & topo-sorts.
- **Declarative building** (`dag::OpGraphBuilder` + `dag::OpRegistry`) — ops are
  auto-collected by type name (`LARK_OP`, Spring-IOC style) and graphs are built
  from a **KV** form (`OpDef{type, deps, id}`) or a small **arrow DSL**
  (`fetch:DataFetchOp -> select:SelectOp -> rank:RankOp`), both parsed with the
  shared `toolkit::dsl` framework.
- **`dag::OpExecutor`** — every op runs **fully asynchronously on the dedicated
  dag thread pool** (`coro::Pools` kDag), composed via `coro::Future` chains;
  emits `metric` `"dag"` `op.*` events; op failures don't poison dependents.

```cpp
lark::coro::Pools pools;
lark::dag::OpExecutor executor(pools.Get(lark::coro::PoolKind::kDag), monitor);

lark::dag::OpGraph graph;
graph.AddOp(std::make_shared<FetchUser>())
     .AddOp(std::make_shared<ScoreUser>())
     .DependsOn("fetch_user", "score_user")
     .AddAspect(std::make_shared<SkipExpensive>());

lark::dag::Context data;
data.Set("user_id", 7);
executor.Execute(graph, data);              // all ops async on the dag pool
```

## Dependencies

- `lark_coro` (coroutine machinery + pools + Future)
- `lark_metric` (unified monitoring)
- `lark_toolkit` (time helpers)

## Build / link

```cmake
add_subdirectory(dag)
target_link_libraries(my_app PRIVATE lark_dag)
```

See `examples/simple_pipeline.cpp`, `tests/test_dag.cpp` (Node layer) and
`tests/test_op.cpp` (Op / aspect layer).
