# lark_dag — DAG Business Execution Framework

A high-performance, fully-asynchronous DAG framework for business node
orchestration. One detached coroutine per node; nodes await their
dependencies' completion events on a coroutine pool, so independent nodes run
concurrently and a small pool never deadlocks on a wide graph.

## Highlights

- **Node / Graph / GraphBuilder / NodeRegistry** — materialize and validate a
  DAG (`dag/dag.h` umbrella header; `LARK_NODE` auto-registration).
- **Executor** — coroutine scheduling across three pools
  (compute / IO / background), with fallback ("兜底") and batch-disable.
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

## Dependencies

- `lark_coro` (coroutine machinery)
- `lark_metric` (unified monitoring)

## Build / link

```cmake
add_subdirectory(dag)
target_link_libraries(my_app PRIVATE lark_dag)
```

See `examples/simple_pipeline.cpp` and `tests/test_dag.cpp`.
