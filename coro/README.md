# lark_coro — Coroutine Primitives & Async Orchestration

C++20 coroutine building blocks plus a CompletableFuture-style orchestration
layer. Used by the rest of the framework (DAG executor, column engine, …).

## Components

| Header | Provides |
|--------|----------|
| `coro/thread_pool.h` | `ThreadPool` + `ThreadPoolConfig` (kind / affinity / priority / backpressure) |
| `coro/pools.h` | `PoolKind` + `Pools` — differentiated worker pools (compute / io / lowload / dag / daemon) |
| `coro/async_event.h` | `AsyncEvent` — one-shot, thread-safe, multi-awaitable completion signal |
| `coro/task.h` | `Task<T>` — lazily-started coroutine with symmetric transfer |
| `coro/fire_and_forget.h` | `FireAndForget` — detached, self-owning coroutine |
| `coro/future.h` | `Future<T>` — CompletableFuture-style async orchestration (Then / ThenAsync / OnError / WhenComplete / AllOf / AnyOf / co_await) |
| `coro/context.h` | `Context` — lock-free field bag for passing data between steps |
| `coro/pipeline.h` | `Pipeline` — context-based step orchestration (Add / AddSync / AddParallel) |
| `coro/batch.h` | `batch` — batch-parallel processing: split a long list into batches, each runs as its own coroutine on a pool |
| `coro/tuner.h` | `ComputeTuner` + `ComputeExecutor` — auto-tune thread vs coroutine, CPU pinning |

## Differentiated pools

The framework owns one pool per purpose, auto-tuned by default:

```cpp
lark::coro::Pools pools;                          // auto sizes
auto& compute = pools.Get(PoolKind::kCompute);    // hardware concurrency
auto& io      = pools.Get(PoolKind::kIo);         // 2x hardware concurrency
auto& low     = pools.Get(PoolKind::kLowLoad);    // 2
auto& dag     = pools.Get(PoolKind::kDag);        // hardware concurrency
auto& daemon  = pools.Get(PoolKind::kDaemon);     // 1
```

`ThreadPoolConfig` overrides size, pins workers to CPUs (`affinity`), sets a
scheduling `priority` hint, and enables `max_queue` backpressure.
`PinCurrentThread(cpu)` pins the calling thread (Linux: hard pin; macOS: kernel
affinity hint).

## CompletableFuture-style orchestration

Functions are passed directly (lambdas — no Java-style anonymous classes):

```cpp
auto f1 = coro::Future<int>::Async([]() -> coro::Task<int> {
            co_await compute.Schedule();
            co_return 21;
          }, compute);
auto f2 = f1.Then([](int v) { return v * 2; });         // -> Future<int>
auto f3 = f1.ThenAsync([](int v) -> coro::Task<int> {   // compose async
            co_await compute.Schedule();
            co_return v + 1;
          });
int x = f3.Get();                                        // block
int y = co_await f3;                                     // or await in a coroutine
```

Supported: `Just / Error / Async / FromThread`, `Then`, `ThenAsync`, `OnError`,
`WhenComplete`, `AllOf`, `AnyOf`, `Wait / Get(timeout)`, and `co_await`.

## Context + Pipeline (business orchestration)

Steps read/write a shared, **lock-free** `Context` (no contention by business
convention) and are composed into a Future chain on a pool:

```cpp
lark::coro::Pipeline pipe(pool, monitor);
pipe.Add("fetch", [&](lark::coro::Context& ctx) -> lark::coro::Task<void> {
  co_await pool.Schedule();
  ctx.Set("a", 21);
  co_return;
});
pipe.AddSync("double", [](lark::coro::Context& ctx) {
  ctx.Set("b", ctx.Get<int>("a") * 2);
});
pipe.AddParallel({ {"p1", step1}, {"p2", step2} });     // distinct fields!
pipe.Run().Wait();
```

## Batch-parallel processing (`coro/batch.h`)

Split a long list (e.g. a `goodsList`) into batches; each batch runs as its own
coroutine on a pool, so several compute coroutines cooperatively compute
attributes / run complex field transforms in parallel. Results stay in the
original order (distinct elements -> no locking by convention).

```cpp
#include "coro/coro.h"
lark::coro::ThreadPool pool(8);

// several compute coroutines transform fields of every goods item
coro::batch::BatchMap(goods, /*batch_size=*/0, /*workers=*/8, pool,
  [](Goods& g) {
    g.discount   = g.price * 0.9;
    g.finalPrice = g.discount + 1.0;
    g.tag        = g.price > 100 ? "expensive" : "cheap";
  }).Wait();

// similar utilities: transform (map to new vector), for-each, reduce
auto tags = coro::batch::BatchTransform<int, std::string>(
                goodsIds, 0, 8, pool, [](int id) { return TagOf(id); }).Get();
long long total = coro::batch::BatchReduce<int, long long>(
                      prices, 0, 8, pool, 0LL,
                      [](long long a, int v) { return a + v; },
                      [](long long a, long long b) { return a + b; }).Get();
```

## Compute auto-tuning

`ComputeTuner` decides per task whether to run as a **coroutine** on the pool, a
**dedicated thread**, or a **CPU-pinned dedicated thread**, based on estimated
duration, CPU-bound-ness and pool load:

```cpp
ComputeExecutor exec;
auto f = exec.Execute(pool, heavyWork, /*estimated_ns=*/20'000'000, /*cpu_bound=*/true);
f.Wait();                                   // heavy -> dedicated thread
auto g = exec.RunPinned(superHeavy);        // super-heavy -> pinned thread
```

## Dependencies

- `lark_metric` (monitoring)
- `lark_toolkit` (time / string helpers)

## Build / link

```cmake
add_subdirectory(coro)
target_link_libraries(my_app PRIVATE lark_coro)
```

Tests: `tests/test_future.cpp`, `tests/test_pools.cpp`.
