# Column Engine v4 — Feed / Compute / Fetch


## English

The column engine is upgraded to a TensorFlow/ggml-inspired **feed → compute → fetch**
architecture with a clean split between the **business layer** and the **execution layer**.

### Core idea

Business code works with rows and modules; the engine computes over columns.

```
 rows  ──feed──▶ columns ──compute (global graph)──▶ columns ──fetch──▶ rows
 (rows)          (columns)      (graph orchestration)      (columns)      (rows)
```

- **feed (row → column)** — `Pipeline::feed(names, rows)` converts row-oriented records
  into columnar tensors and seeds the execution store. Column dtype is inferred
  (all-int → `int64`, any double → `float64`).
- **compute (graph orchestration)** — modules declare their own sub-graphs (pure code or
  the mini-DSL). The framework flattens them into one global `ComputeGraph`,
  wiring modules together with **anonymous/temp nodes** and dependency edges, then
  runs it on the coroutine compute pool (`coro::ThreadPool` + `coro::AsyncEvent`).
- **fetch (column → row)** — `Pipeline::fetch(names)` materializes result columns back
  into business rows.

### Business layer vs execution layer

| Layer | Concern | Files |
|-------|---------|-------|
| **Business** (`biz/`) | WHAT to compute: `Module`, `SubGraph`, DSL, `Pipeline`. A module only knows its inputs, its sub-graph, and its outputs — it never knows how it is joined into the global graph or whether its inputs are ready. | `column/include/column/biz/*` |
| **Execution** (`exec/`) | HOW to compute: `TensorOp` (kernel), `ComputeNode` (binds one op), `ComputeGraph` (DAG + coroutine scheduler). Performance-focused. | `column/include/column/exec/*` |

### Features

- **Multiple numeric types** — `int32 / int64 / float32 / float64` with type
  promotion in binary ops.
- **Quantization (ggml-style)** — `Q8_0` block quantization (32 values per block +
  float scale), plus `dequantize` and a block-quantized `dot` product. Ops:
  `quantize`, `dequantize`, `dot`.
- **Backend factory** — `backend::BackendFactory` registers compute backends by
  name; the CPU backend is built in (SIMD via `compute/simd.h`).
- **DSL** — a tiny expression language for module sub-graphs:
  ```
  scaled     = x * 2
  score      = scaled + y
  mask       = x > 10
  filtered   = filter(score, mask)
  total      = sum(score)
  best       = max(select(mask, score, 0))
  ```
- **Coroutine execution** — independent nodes execute concurrently on the compute
  pool; a node suspends (releasing its worker) while awaiting dependencies, so a
  small pool never deadlocks on a wide graph.
- **ExecutionContext** — per-run context distinguishing the `feed / compute / fetch`
  phases, holding the feed table, the intermediate `TensorStore`, and the fetch table.
- **Generic monitoring** — `metric::Monitor` (unified monitor module) + `StatsCollector` report
  per-module compute time, feed time, fetch time, per-op timings and **CPU pressure
  distribution** (busy / wall × workers).

### Quick start

```cpp
#include "column/column_engine.h"
using namespace lark::column;

auto feature = std::make_shared<biz::Module>("feature");
feature->input("x").input("y").output("score").output("total")
    .from_dsl("score = x * 2 + y\n"
              "total = sum(score)\n");

biz::Pipeline pipeline;                       // CPU backend
pipeline.add_module(feature);
pipeline.compile();

std::vector<Row> rows = {{Cell(int64_t(1)), Cell(int64_t(10))},
                         {Cell(int64_t(2)), Cell(int64_t(20))}};
pipeline.feed({"x", "y"}, rows);              // row -> column
pipeline.compute();                           // graph orchestration
auto out = pipeline.fetch({"score"});         // column -> row
auto total = pipeline.fetch_scalar("total");

std::cout << pipeline.stats().summary();      // monitoring: module / feed timings, CPU pressure
```
