# lark_metric — Unified Monitoring Abstraction

A unified, **pluggable monitoring interface**. Every LARK module (dag, column,
rpc, coro, cache) reports through the same generic `metric::Monitor`; business
code selects the concrete implementation per environment (dev logging, prod
stats, fan-out to an exporter, or a no-op).

## Core types

- `metric::Event` — a generic observation: `source` (`"dag"` / `"column"` /
  `"rpc"` / `"coro"` / `"cache"`), `action` (`"node.success"`, `"cache.hit"`,
  `"feed.end"`, `"rpc.call"`, ...), `subject`, free-form `attrs`, `duration`,
  `ok`.
- `metric::Monitor` — the abstract interface: `Emit(const Event&)`.
- `metric::MonitorFactory` — register / select implementations by name.

## Implementations

| Name | Behavior |
|------|----------|
| `null` | discards everything (production default) |
| `logging` | prints a human-readable line per event |
| `stats` | aggregates per-action counts / durations + error count, `summary()` |
| `composite` | fans out to several child monitors |

## Usage

```cpp
#include "metric/metric.h"

// pick an implementation per environment
auto mon = metric::MonitorFactory::Instance().Create("stats");

mon->Emit(metric::Event{"cache", "cache.hit", "user:42"}
              .attr("backend", "local")
              .attr_ns("elapsed_ns", 1234));

std::cout << std::dynamic_pointer_cast<metric::StatsMonitor>(mon)->summary();
```

### Composite (internal stats + user exporter)

```cpp
auto composite = std::make_shared<metric::CompositeMonitor>();
composite->Add(internal_stats);
composite->Add(user_exporter);
```

## Zero dependencies

`lark_metric` depends only on the standard library; every other module links it.

## Build / link

```cmake
add_subdirectory(metric)
target_link_libraries(my_app PRIVATE lark_metric)
```

Tests: `tests/test_cache.cpp` (`TestMonitorModule`).
