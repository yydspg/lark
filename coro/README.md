# lark_coro — Coroutine Primitives

Standalone C++20 coroutine building blocks used by the rest of the framework.
Zero dependencies (standard library only).

## Components

| Header | Provides |
|--------|----------|
| `coro/thread_pool.h` | `ThreadPool` — fixed-size pool of worker threads that resume coroutine handles |
| `coro/async_event.h` | `AsyncEvent` — one-shot, thread-safe, multi-awaitable completion signal |
| `coro/task.h` | `Task<T>` — lazily-started coroutine with symmetric transfer |
| `coro/fire_and_forget.h` | `FireAndForget` — detached, self-owning coroutine |

## Why it matters

Coroutines that wait on an `AsyncEvent` suspend and **release their worker**, so
a small pool can drive an arbitrarily wide graph without starvation — this is the
machinery behind the DAG executor and the column engine's async compute graph.

## Usage

```cpp
#include "coro/coro.h"

lark::coro::ThreadPool pool(4);
lark::coro::AsyncEvent done;

// A coroutine that hops onto the pool via co_await pool.Schedule() — this
// enqueues it onto a worker (ScheduleAwaiter uses ThreadPool::Enqueue).
// Use a plain function (NOT an immediately-invoked coroutine lambda) to avoid
// a clang codegen bug with coroutine-lambda captures resumed on another thread.
auto task = [&pool, &done]() -> lark::coro::FireAndForget {
  co_await pool.Schedule();   // hop onto a worker
  // ... do work ...
  done.Set();                 // wake every awaiter
  co_return;
};
task();

// ... some other coroutine: co_await done;
```

### Optional monitoring

`ThreadPool::SetMonitor` installs a `metric::Monitor`; the pool then emits
`"coro"` `pool.enqueue` events (off by default, zero overhead).

## Dependencies

- `lark_metric` (optional monitoring)

## Build / link

```cmake
add_subdirectory(coro)
target_link_libraries(my_app PRIVATE lark_coro)
```

Tests: `tests/test_dag.cpp` exercises the pool under load (`TestWideGraphStress`).
