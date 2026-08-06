# Column Engine v4 — Feed / Compute / Fetch (列示计算引擎)

> Bilingual / 双语: [English](#english) · [中文](#中文)

## English

The column engine is upgraded to a TensorFlow/ggml-inspired **feed → compute → fetch**
architecture with a clean split between the **business layer** and the **execution layer**.

### Core idea

Business code works with rows and modules; the engine computes over columns.

```
 rows  ──feed──▶ columns ──compute (global graph)──▶ columns ──fetch──▶ rows
 (行)             (列)         (图节点编排计算)         (列)            (行)
```

- **feed (行转列)** — `Pipeline::feed(names, rows)` converts row-oriented records
  into columnar tensors and seeds the execution store. Column dtype is inferred
  (all-int → `int64`, any double → `float64`).
- **compute (图节点编排计算)** — modules declare their own sub-graphs (pure code or
  the mini-DSL). The framework flattens them into one global `ComputeGraph`,
  wiring modules together with **anonymous/temp nodes** and dependency edges, then
  runs it on the coroutine compute pool (`coro::ThreadPool` + `coro::AsyncEvent`).
- **fetch (列转行)** — `Pipeline::fetch(names)` materializes result columns back
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
- **Generic monitoring** — `monitor::ExecutionMonitor` + `StatsCollector` report
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
pipeline.feed({"x", "y"}, rows);              // 行转列
pipeline.compute();                           // 图节点编排计算
auto out = pipeline.fetch({"score"});         // 列转行
auto total = pipeline.fetch_scalar("total");

std::cout << pipeline.stats().summary();      // 监控: 模块耗时 / feed耗时 / CPU压力
```

## 中文

列示计算部分升级为参考 TensorFlow/ggml 的 **feed → compute → fetch** 架构，
清晰划分 **业务层** 与 **执行层**。

### 核心思路

业务侧按"行 / 模块"思考，引擎按"列"计算：

```
 行  ──feed──▶ 列 ──compute（全局计算图）──▶ 列 ──fetch──▶ 行
```

- **feed（行转列）** — `Pipeline::feed(names, rows)` 将行式记录转为列式张量并注入执行存储；
  列类型自动推断（全 int → `int64`，含 double → `float64`）。
- **compute（图节点编排计算）** — 各 module 只声明自己的 subGraph（纯代码或迷你 DSL）；
  框架用 **匿名节点 / 临时节点** 把它们编入一张全局 `ComputeGraph`，并在协程计算池
  （`coro::ThreadPool` + `coro::AsyncEvent`）上执行，依赖就绪由框架保证。
- **fetch（列转行）** — `Pipeline::fetch(names)` 将结果列还原为业务行。

### 业务层 vs 执行层

| 层 | 关注点 | 文件 |
|----|--------|------|
| **业务层**（`biz/`） | 计算什么：`Module`、`SubGraph`、DSL、`Pipeline`。module 只关心输入、自己的 subGraph、产出；无需感知如何加入全局图、无需感知依赖是否就绪。 | `column/include/column/biz/*` |
| **执行层**（`exec/`） | 怎么算：`TensorOp`（内核）、`ComputeNode`（绑定一个 op）、`ComputeGraph`（DAG + 协程调度），性能优先。 | `column/include/column/exec/*` |

### 特性

- **多数字类型** — `int32 / int64 / float32 / float64`，二元运算自动类型提升。
- **量化（参考 ggml）** — `Q8_0` 块量化（每块 32 个值 + float scale），提供
  `dequantize` 与量化点积 `dot`；算子：`quantize` / `dequantize` / `dot`。
- **后端工厂** — `backend::BackendFactory` 按名注册计算后端；内置 CPU 后端（SIMD）。
- **DSL** — 用于声明 module subGraph 的迷你表达式语言（见上文示例）。
- **协程执行** — 无依赖关系的节点在计算池上并发执行；等待依赖时挂起释放 worker，
  小池子也不会在宽图上死锁。
- **ExecutionContext** — 每次运行的上下文，区分 feed / compute / fetch 三阶段，
  存放 feed 表、中间结果 `TensorStore` 与 fetch 表。
- **通用监控** — `monitor::ExecutionMonitor` + `StatsCollector` 上报每个 module 的计算
  耗时、feed 耗时、fetch 耗时、逐算子耗时与 **CPU 压力分布**（busy / (wall × workers)）。

### 快速开始

```cpp
#include "column/column_engine.h"
using namespace lark::column;

auto feature = std::make_shared<biz::Module>("feature");
feature->input("x").input("y").output("score").output("total")
    .from_dsl("score = x * 2 + y\n"
              "total = sum(score)\n");

biz::Pipeline pipeline;                       // CPU 后端
pipeline.add_module(feature);
pipeline.compile();

std::vector<Row> rows = {{Cell(int64_t(1)), Cell(int64_t(10))},
                         {Cell(int64_t(2)), Cell(int64_t(20))}};
pipeline.feed({"x", "y"}, rows);              // 行转列
pipeline.compute();                           // 图节点编排计算
auto out = pipeline.fetch({"score"});         // 列转行
auto total = pipeline.fetch_scalar("total");

std::cout << pipeline.stats().summary();      // 监控：模块耗时 / feed耗时 / CPU压力
```

See also / 另见: `examples/engine_example.cpp`, `tests/test_engine.cpp`.
