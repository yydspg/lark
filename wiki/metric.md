# metric — Unified Monitoring + Observability

Library: `liblark_metric` · Headers: `metric/include/metric/*`

A unified, pluggable monitoring abstraction plus built-in observability:
flame-graph profiling and null/anomaly probes. Every module reports through
`metric::Monitor`; business picks the concrete implementation.

## Usage

```cpp
#include "metric/metric.h"
using namespace lark::metric;

// pick an implementation per environment (factory / strategy)
auto mon = MonitorFactory::Instance().Create("stats");  // "null"/"logging"/"stats"/"composite"

mon->Emit(Event{"cache", "cache.hit", "user:42"}
              .attr("backend", "local")
              .attr_ns("elapsed_ns", 1234));

std::cout << std::dynamic_pointer_cast<StatsMonitor>(mon)->summary();
```

Composite (internal stats + user exporter) and custom implementations:

```cpp
auto composite = std::make_shared<CompositeMonitor>();
composite->Add(internal_stats);
composite->Add(user_exporter);
```

### Flame-graph profiling

```cpp
profile::FlameGraphProfiler prof;
prof.Enable();
void Compute() {
  PROFILE_SCOPE(prof, "row");          // scope names must NOT contain "::"
  // ...
}
std::cout << prof.TextFlameGraph();        // indented flame graph
std::cout << prof.CollapsedStacks();       // "row;batch 60" (flamegraph.pl)
```

### Probes

```cpp
int* p = nullptr;
if (!probe::NotNull(p, "p", monitor)) { /* null detected */ }
int& r = probe::CheckNotNull(p, "p", monitor);   // throws on null
probe::Check(cond, "invariant", monitor, /*throw=*/true);
probe::AnomalyDetector latency(monitor, 3.0);
if (latency.Feed(50.0, "latency")) { /* statistical outlier */ }
```

## Caveats

- **Thread safety**: `Monitor` callbacks can fire from any worker; built-in
  implementations lock internally. A custom monitor must be thread-safe.
- **Event schema**: `source` = emitting module (`"dag"`/`"column"`/`"rpc"`/
  `"coro"`/`"cache"`/`"metric"`), `action` = verb (`"node.success"`,
  `"cache.hit"`, `"feed.end"`, `"rpc.call"`, ...), `subject` = target (node id /
  key / method), `attrs` = stringly-typed details, `duration`/`ok`. Consumers
  filter by `source`+`action`.
- **`StatsMonitor`** only aggregates counts/durations per action + error count —
  it is generic. Domain stats (e.g. column `RunStats`, dag `ExecutionStats`) are
  dedicated `Monitor` implementations in their modules.
- **Flame-graph scope names must not contain `::`** (the path delimiter).
- `probe::AnomalyDetector` is an online (Welford) estimator — it is
  hypersensitive with tiny samples; start from a warm baseline or raise `k`.

## Implementation

- **`Event`**: `source/action/subject` + `attrs` map + `duration` + `ok` +
  timestamp.
- **`Monitor`**: single abstract method `Emit(const Event&)`.
- **`NullMonitor`** discards; **`LoggingMonitor`** prints a line;
  **`StatsMonitor`** keeps per-action count/duration maps and an error counter;
  **`CompositeMonitor`** snapshots its children and fans out.
- **`MonitorFactory`** registers implementations by name (`null`/`logging`/
  `stats`/`composite`, plus user-registered).
- **FlameGraphProfiler**: thread-local scope-name stack builds full `::`-joined
  paths; each completed scope records `(path, elapsed)` into a
  `map<path, {total, count}>`. `Snapshot()` rebuilds the tree and derives
  `self = total − Σ children`; `TextFlameGraph`/`CollapsedStacks` render it.
  `Sampler` periodically snapshots and emits `profile.sample` events.
- **AnomalyDetector**: Welford online mean/variance; flags values farther than
  `k·σ` from the running mean.

## Architecture

```
metric (zero deps) ─── consumed by every module
   ├─ Monitor (abstract) ◀── dag / column / rpc / coro / cache emit Events
   ├─ MonitorFactory (strategy: pick implementation)
   ├─ profile/  flame-graph profiling
   └─ probe/    null/invariant/anomaly detection
```

`metric` depends only on the standard library and is the observability
backbone of the whole framework.
