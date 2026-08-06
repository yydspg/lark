# coro — Coroutine Primitives & Async Orchestration

Library: `liblark_coro` · Headers: `coro/include/coro/*`

## Usage

### Low-level primitives

```cpp
#include "coro/coro.h"

lark::coro::ThreadPool pool(4);            // 4 worker threads

lark::coro::AsyncEvent done;               // one-shot completion signal
// coroutines that need a worker hop onto the pool:
auto task = [&pool, &done]() -> lark::coro::FireAndForget {
  co_await pool.Schedule();                // enqueue self onto the pool
  // ... do work ...
  done.Set();                              // wake every awaiter
  co_return;
};
task();
// ... somewhere else: co_await done;
```

### CompletableFuture-style orchestration

```cpp
auto f1 = coro::Future<int>::Async([]() -> coro::Task<int> {
            co_await pool.Schedule();
            co_return 21;
          }, pool);
auto f2 = f1.Then([](int v) { return v * 2; });        // sync transform
auto f3 = f1.ThenAsync([](int v) -> coro::Task<int> {  // async compose
            co_await pool.Schedule();
            co_return v + 1;
          });
int x = f3.Get();                                       // block
int y = co_await f3;                                    // await in a coroutine
```

Supported: `Just / Error / Async / FromThread`, `Then`, `ThenAsync`, `OnError`,
`WhenComplete`, `AllOf`, `AnyOf`, `Wait / Get(timeout)`, `co_await`.

### Context + Pipeline (business orchestration)

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
pipe.AddParallel({ {"p1", step1}, {"p2", step2} });   // distinct fields!
pipe.Run().Wait();
```

### Differentiated pools + auto-tuning

```cpp
lark::coro::Pools pools;                       // auto sizes per kind
auto& compute = pools.Get(PoolKind::kCompute); // hw concurrency
auto& dag     = pools.Get(PoolKind::kDag);

ComputeExecutor exec;                          // auto thread-vs-coroutine
auto f = exec.Execute(compute, heavyWork, /*ns*/20'000'000, /*cpu_bound*/true);
f.Wait();                                      // heavy -> dedicated thread
auto g = exec.RunPinned(superHeavy);           // super-heavy -> pinned thread
```

### Batch-parallel

```cpp
coro::batch::BatchMap(goods, 0, 8, pool, [](Goods& g) {
  g.discount = g.price * 0.9; g.finalPrice = g.discount + 1.0;
}).Wait();                                     // each batch = one coroutine
```

## Caveats

- **Do not use immediately-invoked coroutine lambdas** for fire-and-forget work
  (`[...]() -> FireAndForget {...}()`). On the shipped toolchain (clang) their
  captures are corrupted when the coroutine is resumed on another thread
  (ASan: stack-use-after-scope). Use a **plain function** returning
  `FireAndForget`, or `Future::Async`/`Pipeline` (which use function-template
  continuations). Task-returning coroutine lambdas are fine.
- **`Context` is lock-free by convention**: sequential steps never overlap, and
  parallel branches must write *distinct* fields. Concurrent writes to the same
  field are a data race — the framework will not catch it.
- **`Pipeline` / non-blocking `ExecuteAsync` capture the graph/context by
  reference** — keep them alive until the returned `Future` resolves.
- **`Future::Async` requires a pool that outlives the future.**
- `Pools` are fixed-size; the compute tuner only estimates — it never
  guarantees a thread for a "heavy" task if the decision says coroutine.

## Implementation

- **ThreadPool**: `N` worker threads pull `coroutine_handle<>` from a deque.
  `co_await pool.Schedule()` enqueues the awaiting handle and suspends — a
  worker resumes it, so suspended coroutines never hold a worker (no starvation
  on wide graphs). `ThreadPoolConfig` sets size, CPU `affinity`
  (Linux `sched_setaffinity`, macOS `THREAD_AFFINITY_POLICY` hint), priority and
  a `max_queue` backpressure knob.
- **AsyncEvent**: a single `std::atomic<void*>` whose states are `nullptr`
  (unset) / `this` (set) / head of an intrusive Awaiter list. `Set()` exchanges
  to `this` and resumes waiters inline (on the setting thread).
- **Task**: lazy coroutine (initial suspend) with symmetric transfer — awaiting
  a Task transfers control into it; on completion its final awaiter resumes the
  continuation.
- **Future**: a `FutureState<T>` (mutex+cv+optional result+AsyncEvent) shared
  by the future and its continuations. Combinators spawn **coroutine function
  templates** (`detail::RunThen`, `RunThenAsync`, ...) that `co_await` the
  source state's AsyncEvent, apply the user function, and resolve the output
  state. `AllOf` counts down an atomic; `AnyOf` uses a CAS winner flag.
- **Pools**: owns five `ThreadPool`s; sizes auto from `DefaultThreadsFor`.
- **ComputeTuner**: `Decide(duration, cpu_bound, utilization)` → coroutine /
  thread / pinned thread; `ComputeExecutor` runs accordingly (coroutine path
  via `Future::Async`, thread path via `Future<void>::FromThread`).
- **batch**: `RunBatches` splits `[0,n)` into batches, launches one
  `Future::Async` per batch (each writes distinct elements), and joins with
  `AllOf`.

## Architecture

```
metric ─┐
toolkit ─┴─▶ coro ─▶ dag / column / cache
```

`coro` depends on `metric` + `toolkit` and is depended on by dag, column and
cache. It is the foundation layer: primitives (`ThreadPool` / `AsyncEvent` /
`Task` / `FireAndForget`) at the bottom, orchestration (`Future` / `Context` /
`Pipeline`) and system services (`Pools` / `ComputeTuner` / `batch`) above.
