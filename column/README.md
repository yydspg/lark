# lark_column — Column Engine (feed / compute / fetch)

A TensorFlow/ggml-inspired columnar computing engine with a clean split between
the **business layer** and the **execution layer**.

```
 rows ──feed──▶ columns ──compute (global graph)──▶ columns ──fetch──▶ rows
```

## Layers

| Layer | Concern | Headers |
|-------|---------|---------|
| Business (`biz/`) | WHAT to compute: `Module`, `SubGraph`, mini-DSL, `Pipeline`. A module only declares inputs / sub-graph / outputs — it never knows how it is joined into the global graph or whether its inputs are ready. | `column/biz/*` |
| Execution (`exec/`) | HOW to compute: `TensorOp` (kernel), `ComputeNode` (binds one op), `ComputeGraph` (DAG + coroutine scheduler). | `column/exec/*` |
| Backend (`backend/`) | compute-backend factory (CPU implemented, SIMD). | `column/backend/backend.h` |

## Features

- **feed / compute / fetch** — `Pipeline::feed(names, rows)` (row → column),
  `Pipeline::compute()` (graph orchestration on the coroutine pool),
  `Pipeline::fetch(names)` (column → row).
- **Anonymous/temp nodes** — the framework wires modules together with
  `@in` / `@out` / `@dep` boundary nodes and module-local temp columns.
- **Total-graph orchestration with placeholders** — modules compile in
  registration order; when a module references a column (or a `depends_on`
  tail) of a module compiled later, an anonymous **placeholder node** stands in
  (placeholder) so the graph stays buildable, and is **replaced** (final replacement) by the
  real producer once it compiles. Cross-module cycles are still caught at
  `ComputeGraph::Finalize`.
- **DSL** — declare sub-graphs in a tiny expression language (parsed with the
  shared `toolkit::dsl` framework, reused by the dag arrow DSL):
  ```
  scaled = x * 2
  score  = scaled + y
  mask   = x > 10
  filtered = filter(score, mask)
  total  = sum(score)
  best   = max(select(mask, score, 0))
  ```
- **Multi-dtype tensors** — `int32 / int64 / float32 / float64` with type
  promotion.
- **Quantization (ggml-style)** — `Q8_0` block quantization, `dequantize`, and
  block-quantized `dot`.
- **ExecutionContext** — per-run context distinguishing `feed / compute /
  fetch` phases, holding the feed table, the intermediate `TensorStore`, and
  the fetch table.
- **Unified monitoring** — the pipeline / compute graph emit `metric` `"column"`
  events (phases, node end/error, module end, CPU pressure); `StatsCollector`
  aggregates them into `RunStats` with a `summary()`.

## Quick start

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

std::cout << pipeline.stats().summary();      // monitoring
```

## Dependencies

- `lark_coro` (async execution)
- `lark_metric` (unified monitoring)

## Build / link

```cmake
add_subdirectory(column)
target_link_libraries(my_app PRIVATE lark_column)
```

See `examples/engine_example.cpp` and `tests/test_engine.cpp`.
