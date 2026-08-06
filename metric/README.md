# lark_metric — Unified Monitoring + Observability

A unified, **pluggable monitoring interface**. Every LARK module (dag, column,
rpc, coro, cache) reports through the same generic `metric::Monitor`; business
code selects the concrete implementation per environment (dev logging, prod
stats, fan-out to an exporter, or a no-op).

On top of that interface the module also ships built-in **observability**:
flame-graph style CPU profiling and null-pointer / anomaly probes.

## Core types

- `metric::Event` — a generic observation: `source` (`"dag"` / `"column"` /
  `"rpc"` / `"coro"` / `"cache"` / `"metric"`), `action` (`"node.success"`,
  `"cache.hit"`, `"feed.end"`, `"rpc.call"`, `"probe.null"`, ...), `subject`,
  free-form `attrs`, `duration`, `ok`.
- `metric::Monitor` — the abstract interface: `Emit(const Event&)`.
- `metric::MonitorFactory` — register / select implementations by name.

## Implementations

| Name | Behavior |
|------|----------|
| `null` | discards everything (production default) |
| `logging` | prints a human-readable line per event |
| `stats` | aggregates per-action counts / durations + error count, `summary()` |
| `composite` | fans out to several child monitors |

## Flame-graph profiling (`metric/profile.h`)

The C++ equivalent of a Java flame-graph framework: instrument hot paths with
RAII scopes and the profiler aggregates every scope instance into a call tree
(self / total time + entry counts), renderable as a text flame graph or as
collapsed stacks for `flamegraph.pl`-style tools. Ideal for load-testing / debug
performance analysis.

```cpp
#include "metric/metric.h"
using namespace lark::metric;

profile::FlameGraphProfiler prof;
prof.Enable();

void Compute() {
  PROFILE_SCOPE(prof, "row");          // scope names must NOT contain "::"
  // ... work ...
}

std::cout << prof.TextFlameGraph();       // indented flame graph
std::cout << prof.CollapsedStacks();      // "row;batch 60" (flamegraph.pl)
```

- `FlameGraphProfiler` — thread-safe aggregation; `Snapshot()` returns the call
  tree (`Frame` with `self_ns` / `total_ns` / `count`).
- `Scope` / `PROFILE_SCOPE(profiler, name)` — RAII timing; full call paths are
  built automatically from the nested scopes.
- `Sampler` — optional background thread that streams periodic
  `"profile.sample"` events into a `Monitor` during load tests.

## Probes (`metric/probe.h`)

Detect the runtime problems you'd hunt during debugging — null pointers, broken
invariants, statistical outliers — and surface them as `metric` `probe.*`
events.

```cpp
int* p = nullptr;
if (!probe::NotNull(p, "p", monitor)) { /* null detected */ }
int& r = probe::CheckNotNull(p, "p", monitor);   // throws on null
probe::Check(cond, "invariant", monitor, /*throw=*/true);

probe::AnomalyDetector latency(monitor, 3.0);    // k * stddev threshold
if (latency.Feed(50.0, "latency")) { /* outlier */ }
```

## Dependencies

- `lark_toolkit` (string / time helpers)

## Build / link

```cmake
add_subdirectory(metric)
target_link_libraries(my_app PRIVATE lark_metric)
```

Tests: `tests/test_metric.cpp`, `tests/test_cache.cpp` (`TestMonitorModule`).
See `examples/metric_example.cpp`.
