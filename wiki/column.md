# column — Column Engine (feed / compute / fetch)

Library: `liblark_column` · Headers: `column/include/column/*`

A TensorFlow/ggml-inspired columnar computing engine split into a **business
layer** (what to compute) and an **execution layer** (how to compute).

## Usage

```cpp
#include "column/column_engine.h"
using namespace lark::column;

auto feature = std::make_shared<biz::Module>("feature");
feature->input("x").input("y").output("score").output("total")
    .from_dsl("score = x * 2 + y\n"
              "total = sum(score)\n");            // business rule DSL

biz::Pipeline pipeline;                           // CPU backend
pipeline.add_module(feature);
pipeline.compile();

std::vector<Row> rows = {{Cell(int64_t(1)), Cell(int64_t(10))},
                         {Cell(int64_t(2)), Cell(int64_t(20))}};
pipeline.feed({"x", "y"}, rows);                  // row → column
pipeline.compute();                               // graph orchestration on the pool
auto out = pipeline.fetch({"score"});             // column → row
auto total = pipeline.fetch_scalar("total");
std::cout << pipeline.stats().summary();          // monitoring
```

### Module wiring — forward references (total-graph orchestration)

Modules compile in **registration order**; a module registered first may consume
a later module's output — the framework creates an anonymous **placeholder
node**, then **replaces** it once the producer compiles:

```cpp
auto a = std::make_shared<biz::Module>("a");
a->input("x").input("b_out").output("final").from_dsl("final = b_out + x");
auto b = std::make_shared<biz::Module>("b");
b->input("x").output("b_out").from_dsl("b_out = x * 2");
biz::Pipeline p;
p.add_module(a).add_module(b);      // a FIRST, depends on b
p.compile();                        // placeholder created, then replaced
p.feed({"x"}, {{Cell(int64_t(3))}}); p.compute();
CHECK(p.fetch_scalar("final") == 9); // b_out=6, final=9
```

## Caveats

- **Module order is free** (placeholders resolve forward references), but a
  column written by two modules, or written twice inside one module, is a
  compile error.
- **Feed ports** are columns no module produces; they are seeded before compute
  and need no dependency edge.
- **`Context`/store writes** are race-free because each tensor is produced by
  exactly one op and consumed after it — the graph pre-registers every output
  name (`EnsureNames`) so concurrent op writes never rehash the store.
- **Fetch requires equal-length columns** — a scalar result mixed with a vector
  in one `fetch(names)` call throws; fetch them separately.
- **Quantized tensors** (`DType::kQ8_0`) cannot be `filter`ed and store block
  metadata; `size()` is the logical element count.
- Monitoring is pluggable: attach any `metric::Monitor` via
  `pipeline.set_monitor(...)`; a `StatsCollector` is always installed internally
  so `pipeline.stats()` works.

## Implementation

### Execution layer

- **Tensor**: type-erased `std::vector<uint8_t>` buffer over `int32/int64/
  float32/float64/Q8_0`. `Q8_0` stores ggml-style blocks (32 int8 + float
  scale); `quantize/dequantize/dot_q8_0` operate on blocks.
- **TensorStore**: name → Tensor map with **no row-count invariant**, so
  `filter`/`reduce` produce different lengths. `EnsureNames` pre-registers all
  output names before a parallel run; `set()` on an existing key replaces the
  value in place (no rehash) — that is what makes concurrent op writes safe.
- **TensorOp / ComputeNode**: an op declares `inputs/outputs` and implements
  `compute(OpContext&)` reading/writing the store; a `ComputeNode` binds one
  op. Op kernels dispatch over the scalar dtypes via `for_each_scalar_type` and
  are split into per-family translation units (`ops_binary/unary/reduce/
  compare/special.cpp`).
- **ComputeGraph**: DAG of nodes; `Finalize` topo-sorts (Kahn) and detects
  cycles. `ExecuteAsync` spawns one `coro::FireAndForget` per node that awaits
  dependency `AsyncEvent`s and runs the op on the compute pool. Placeholders
  (`AddPlaceholder`/`ReplacePlaceholder`) stand in for forward references and
  are rewired+removed before execution.

### Business layer

- **Module** declares inputs / sub-graph / outputs only.
- **PipelineCompiler** (total-graph orchestration): pass 1 collects all produced columns; pass 2
  compiles in registration order, resolving anonymous `@in` / `@out` / `@dep`
  boundary nodes, module-local temp columns, and placeholders for forward
  references. The expression DSL is parsed with the shared `toolkit::dsl`.
- **ExecutionContext** tracks `feed / compute / fetch` phases and timing.

## Architecture

```
business (biz/: Module, DSL, PipelineCompiler, Pipeline)      WHAT
       │ compiles to
execution (exec/: TensorOp, ComputeNode, ComputeGraph)        HOW
       │ runs on
coro pools (compute pool) ──▶ metrics (column.* events)
```

Backends (`backend::BackendFactory`, CPU implemented) materialize `TensorOp`s
from `OpSpec`s. `column` depends on `coro` + `metric` + `toolkit`.
