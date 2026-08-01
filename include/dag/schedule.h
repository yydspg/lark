#pragma once

#include "dag/coro/fire_and_forget.h"
#include "dag/coro/thread_pool.h"
#include "dag/i_context.h"

namespace lark {

// ---------------------------------------------------------------------------
// Orchestration primitives for business code.
//
// Nodes run fully asynchronously: the framework spawns one coroutine per node,
// and each node's body may itself need to orchestrate sub-tasks (parallel
// fetches, CPU-heavy preprocessing, ...). The helpers below integrate
// seamlessly with the framework's coroutine machinery.
//
// Two primitives are provided:
//
//   * Schedule(ctx, PoolKind) -- an awaitable that hops the current coroutine
//     onto the chosen pool. Use it inside a node coroutine to move work
//     between pools:
//
//         Task<void> MyNode::Run(IContext& ctx) override {
//           co_await Schedule(ctx, PoolKind::kCompute);  // hop to compute pool
//           HeavyCpuWork();
//           co_await Schedule(ctx, PoolKind::kIo);       // hop back for I/O
//           co_await FetchFromNetwork();
//           co_return;
//         }
//
//   * Spawn(ctx, PoolKind, callable) -- fire-and-forget a callable on the
//     chosen pool. The callable runs to completion independently; use it to
//     kick off parallel work whose result is collected later (typically via
//     the Context):
//
//         Task<void> MyNode::Run(IContext& ctx) override {
//           Spawn(ctx, PoolKind::kIo, [&ctx] () -> coro::FireAndForget {
//             auto data = co_await FetchA();
//             lark::Set(ctx, "a", data);
//             co_return;
//           }());
//           Spawn(ctx, PoolKind::kIo, [&ctx] () -> coro::FireAndForget {
//             auto data = co_await FetchB();
//             lark::Set(ctx, "b", data);
//             co_return;
//           }());
//           // ... do other work; the spawns run concurrently on the IO pool.
//           co_return;
//         }
//
// Both helpers reach the pools via IContext::GetPool, so they work with any
// IContext implementation that exposes pools (DefaultContext does).
// ---------------------------------------------------------------------------

// Awaitable that resumes the current coroutine on the chosen pool.
class ScheduleAwaiter {
 public:
  ScheduleAwaiter(IContext& ctx, PoolKind kind)
      : pool_(ctx.GetPool(kind)) {}

  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> handle) noexcept {
    pool_.Enqueue(handle);
  }
  void await_resume() const noexcept {}

 private:
  ThreadPool& pool_;
};

// Hop the current coroutine onto the pool of the given kind.
inline ScheduleAwaiter Schedule(IContext& ctx, PoolKind kind) {
  return ScheduleAwaiter{ctx, kind};
}

// Fire-and-forget a callable's result coroutine on the chosen pool. The
// callable must return coro::FireAndForget (or any self-owning coroutine).
// The returned handle is enqueued immediately; the caller does not wait for
// it.
inline void Spawn(IContext& ctx, PoolKind kind,
                  std::coroutine_handle<> handle) {
  ctx.GetPool(kind).Enqueue(handle);
}

}  // namespace lark
