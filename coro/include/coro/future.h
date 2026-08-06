// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// CompletableFuture-style async orchestration built on the coroutine primitives.
//
//   auto f1 = coro::Future<int>::Async([]() -> coro::Task<int> {
//               co_await pool.Schedule();
//               co_return 21;
//             }, pool);
//   auto f2 = f1.Then([](int v) { return v * 2; });          // -> Future<int>
//   auto f3 = f1.ThenAsync([](int v) -> coro::Task<int> {    // compose async
//               co_await pool.Schedule();
//               co_return v + 1;
//             });
//   int x = f3.Get();                                        // block
//
//   int y = co_await f3;                                     // or await in a coroutine
//
// Functions are passed directly (C++ lambdas — no Java-style anonymous
// classes). For data passing between composed steps see Context / Pipeline.
//
// NOTE: continuations are implemented as coroutine function templates (not
// immediately-invoked coroutine lambdas) — this avoids a clang codegen bug
// where coroutine-lambda captures are corrupted when the coroutine is resumed
// on another thread.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "coro/async_event.h"
#include "coro/fire_and_forget.h"
#include "coro/task.h"
#include "coro/thread_pool.h"

namespace lark::coro {

// ─────────────────────────────────────────────────────────────────────────────
// detail::FutureState — shared completion state (result + error + signals).
// ─────────────────────────────────────────────────────────────────────────────
namespace detail {

template <typename T>
struct FutureState {
  mutable std::mutex mutex;
  mutable std::condition_variable cv;
  bool resolved = false;
  std::optional<T> value;
  std::exception_ptr error;
  coro::AsyncEvent done;

  void ResolveValue(T v) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (resolved) return;
      value = std::move(v);
      resolved = true;
    }
    done.Set();
    cv.notify_all();
  }

  void ResolveError(std::exception_ptr e) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (resolved) return;
      error = std::move(e);
      resolved = true;
    }
    done.Set();
    cv.notify_all();
  }

  bool WaitFor(std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, timeout, [this] { return resolved; });
  }
  void Wait() const {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this] { return resolved; });
  }
  T Get() const {
    Wait();
    std::lock_guard<std::mutex> lock(mutex);
    if (error) std::rethrow_exception(error);
    return *value;
  }
};

template <>
struct FutureState<void> {
  mutable std::mutex mutex;
  mutable std::condition_variable cv;
  bool resolved = false;
  std::exception_ptr error;
  coro::AsyncEvent done;

  void ResolveValue() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (resolved) return;
      resolved = true;
    }
    done.Set();
    cv.notify_all();
  }

  void ResolveError(std::exception_ptr e) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (resolved) return;
      error = std::move(e);
      resolved = true;
    }
    done.Set();
    cv.notify_all();
  }

  bool WaitFor(std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, timeout, [this] { return resolved; });
  }
  void Wait() const {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this] { return resolved; });
  }
  void Get() const {
    Wait();
    std::lock_guard<std::mutex> lock(mutex);
    if (error) std::rethrow_exception(error);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Continuation coroutines. Function templates (parameters, not lambda
// captures) — see the note at the top of this header.
// ─────────────────────────────────────────────────────────────────────────────

// Run a producer task on the pool and resolve the output state.
template <typename T, typename Producer>
coro::FireAndForget RunProducer(std::shared_ptr<FutureState<T>> state,
                                Producer producer, ThreadPool& pool) {
  co_await pool.Schedule();
  try {
    if constexpr (std::is_void_v<T>) {
      co_await producer();
      state->ResolveValue();
    } else {
      auto value = co_await producer();
      state->ResolveValue(std::move(value));
    }
  } catch (...) {
    state->ResolveError(std::current_exception());
  }
  co_return;
}

// Then (value): fn(const T&) -> U
template <typename T, typename U, typename Fn>
coro::FireAndForget RunThen(std::shared_ptr<FutureState<T>> self,
                            std::shared_ptr<FutureState<U>> out, Fn fn) {
  co_await self->done;
  if (self->error) {
    out->ResolveError(self->error);
    co_return;
  }
  try {
    if constexpr (std::is_void_v<U>) {
      fn(*self->value);
      out->ResolveValue();
    } else {
      out->ResolveValue(fn(*self->value));
    }
  } catch (...) {
    out->ResolveError(std::current_exception());
  }
  co_return;
}

// Then (void self): fn() -> U
template <typename U, typename Fn>
coro::FireAndForget RunThenVoid(std::shared_ptr<FutureState<void>> self,
                                std::shared_ptr<FutureState<U>> out, Fn fn) {
  co_await self->done;
  if (self->error) {
    out->ResolveError(self->error);
    co_return;
  }
  try {
    if constexpr (std::is_void_v<U>) {
      fn();
      out->ResolveValue();
    } else {
      out->ResolveValue(fn());
    }
  } catch (...) {
    out->ResolveError(std::current_exception());
  }
  co_return;
}

// ThenAsync (value): fn(const T&) -> Future<U> / Task<U>
template <typename T, typename U, typename Fn>
coro::FireAndForget RunThenAsync(std::shared_ptr<FutureState<T>> self,
                                 std::shared_ptr<FutureState<U>> out, Fn fn) {
  co_await self->done;
  if (self->error) {
    out->ResolveError(self->error);
    co_return;
  }
  try {
    auto next = fn(*self->value);
    if constexpr (std::is_void_v<U>) {
      co_await std::move(next);
      out->ResolveValue();
    } else {
      out->ResolveValue(co_await std::move(next));
    }
  } catch (...) {
    out->ResolveError(std::current_exception());
  }
  co_return;
}

// ThenAsync (void self): fn() -> Future<U> / Task<U>
template <typename U, typename Fn>
coro::FireAndForget RunThenAsyncVoid(std::shared_ptr<FutureState<void>> self,
                                     std::shared_ptr<FutureState<U>> out,
                                     Fn fn) {
  co_await self->done;
  if (self->error) {
    out->ResolveError(self->error);
    co_return;
  }
  try {
    auto next = fn();
    if constexpr (std::is_void_v<U>) {
      co_await std::move(next);
      out->ResolveValue();
    } else {
      out->ResolveValue(co_await std::move(next));
    }
  } catch (...) {
    out->ResolveError(std::current_exception());
  }
  co_return;
}

// OnError (value): fn(exception_ptr) -> T
template <typename T, typename Fn>
coro::FireAndForget RunOnError(std::shared_ptr<FutureState<T>> self,
                               std::shared_ptr<FutureState<T>> out, Fn fn) {
  co_await self->done;
  if (!self->error) {
    out->ResolveValue(*self->value);
    co_return;
  }
  try {
    out->ResolveValue(fn(self->error));
  } catch (...) {
    out->ResolveError(std::current_exception());
  }
  co_return;
}

// OnError (void): fn(exception_ptr) -> void
template <typename Fn>
coro::FireAndForget RunOnErrorVoid(std::shared_ptr<FutureState<void>> self,
                                   std::shared_ptr<FutureState<void>> out,
                                   Fn fn) {
  co_await self->done;
  if (!self->error) {
    out->ResolveValue();
    co_return;
  }
  try {
    fn(self->error);
    out->ResolveValue();
  } catch (...) {
    out->ResolveError(std::current_exception());
  }
  co_return;
}

// WhenComplete (value): fn(const std::optional<T>&, exception_ptr)
template <typename T, typename Fn>
coro::FireAndForget RunWhenComplete(std::shared_ptr<FutureState<T>> self,
                                    std::shared_ptr<FutureState<T>> out,
                                    Fn fn) {
  co_await self->done;
  try {
    fn(self->value, self->error);
  } catch (...) {
    out->ResolveError(std::current_exception());
    co_return;
  }
  if (self->error) {
    out->ResolveError(self->error);
  } else {
    out->ResolveValue(*self->value);
  }
  co_return;
}

// WhenComplete (void): fn(exception_ptr)
template <typename Fn>
coro::FireAndForget RunWhenCompleteVoid(std::shared_ptr<FutureState<void>> self,
                                        std::shared_ptr<FutureState<void>> out,
                                        Fn fn) {
  co_await self->done;
  try {
    fn(self->error);
  } catch (...) {
    out->ResolveError(std::current_exception());
    co_return;
  }
  if (self->error) {
    out->ResolveError(self->error);
  } else {
    out->ResolveValue();
  }
  co_return;
}

// AllOf (value): collect one result slot when a future resolves.
template <typename T>
coro::FireAndForget RunAllOf(std::shared_ptr<FutureState<T>> state,
                             std::shared_ptr<FutureState<std::vector<T>>> out,
                             std::shared_ptr<std::vector<T>> result,
                             std::shared_ptr<std::atomic<std::size_t>> remaining,
                             std::size_t index) {
  co_await state->done;
  if (state->error) {
    out->ResolveError(state->error);
    co_return;
  }
  (*result)[index] = *state->value;
  if (remaining->fetch_sub(1) == 1) {
    out->ResolveValue(std::move(*result));
  }
  co_return;
}

// AllOf (void): count down when a future resolves.
inline coro::FireAndForget RunAllOfVoid(std::shared_ptr<FutureState<void>> state,
                                 std::shared_ptr<FutureState<void>> out,
                                 std::shared_ptr<std::atomic<std::size_t>> remaining) {
  co_await state->done;
  if (state->error) {
    out->ResolveError(state->error);
    co_return;
  }
  if (remaining->fetch_sub(1) == 1) out->ResolveValue();
  co_return;
}

// AnyOf (value): first success wins -> (index, value); all-failed -> error.
template <typename T>
coro::FireAndForget RunAnyOf(std::shared_ptr<FutureState<T>> state,
                             std::shared_ptr<FutureState<std::pair<std::size_t, T>>> out,
                             std::shared_ptr<std::atomic<bool>> winner,
                             std::shared_ptr<std::atomic<std::size_t>> failed,
                             std::shared_ptr<std::exception_ptr> first_error,
                             std::size_t count, std::size_t index) {
  co_await state->done;
  if (state->error) {
    if (!winner->load()) {
      *first_error = state->error;
    }
    if (failed->fetch_add(1) + 1 == count && !winner->load()) {
      out->ResolveError(*first_error);
    }
    co_return;
  }
  bool expected = false;
  if (winner->compare_exchange_strong(expected, true)) {
    out->ResolveValue(std::make_pair(index, *state->value));
  }
  co_return;
}

// AnyOf (void): first completion wins.
inline coro::FireAndForget RunAnyOfVoid(std::shared_ptr<FutureState<void>> state,
                                 std::shared_ptr<FutureState<void>> out,
                                 std::shared_ptr<std::atomic<bool>> winner,
                                 std::shared_ptr<std::atomic<std::size_t>> failed,
                                 std::size_t count) {
  co_await state->done;
  if (state->error) {
    if (failed->fetch_add(1) + 1 == count && !winner->load()) {
      out->ResolveError(state->error);
    }
    co_return;
  }
  bool expected = false;
  if (winner->compare_exchange_strong(expected, true)) out->ResolveValue();
  co_return;
}

}  // namespace detail

template <typename T>
class Future;

// Trait: value type carried by Future<T> / Task<T>.
template <typename T>
struct AwaitValue;
template <typename T>
struct AwaitValue<Future<T>> {
  using type = T;
};
template <typename T>
struct AwaitValue<Task<T>> {
  using type = T;
};

// ─────────────────────────────────────────────────────────────────────────────
// Future<T>
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
class Future {
 public:
  using ValueType = T;
  using Producer = std::function<Task<T>()>;

  Future() = default;
  Future(const Future&) = default;
  Future& operator=(const Future&) = default;

  bool valid() const { return state_ != nullptr; }

  static Future Just(T value) {
    Future f;
    f.state_ = std::make_shared<detail::FutureState<T>>();
    f.state_->ResolveValue(std::move(value));
    return f;
  }

  static Future Error(std::exception_ptr error) {
    Future f;
    f.state_ = std::make_shared<detail::FutureState<T>>();
    f.state_->ResolveError(std::move(error));
    return f;
  }

  // Run `producer` on `pool`; its result (value or exception) resolves this
  // future. `producer` is a coroutine Task, passed directly as a lambda.
  static Future Async(Producer producer, ThreadPool& pool) {
    Future f;
    f.state_ = std::make_shared<detail::FutureState<T>>();
    detail::RunProducer<T>(f.state_, std::move(producer), pool);
    return f;
  }

  // ---- blocking access --------------------------------------------------

  void Wait() const { RequireState()->Wait(); }
  bool WaitFor(std::chrono::milliseconds timeout) const {
    return RequireState()->WaitFor(timeout);
  }
  T Get() const { return RequireState()->Get(); }
  T Get(std::chrono::milliseconds timeout) const {
    auto s = RequireState();
    if (!s->WaitFor(timeout)) throw std::runtime_error("Future::Get: timed out");
    return s->Get();
  }

  // ---- orchestration (functions passed directly) ------------------------

  // Transform: fn(const T&) -> U, run when this completes successfully.
  template <typename Fn>
  auto Then(Fn&& fn) const -> Future<typename std::invoke_result_t<Fn, const T&>> {
    using U = typename std::invoke_result_t<Fn, const T&>;
    auto out = std::make_shared<detail::FutureState<U>>();
    detail::RunThen<T, U>(RequireState(), out, std::forward<Fn>(fn));
    return Future<U>::FromState(out);
  }

  // Compose: fn(const T&) -> Future<U> (or Task<U>), awaited when this
  // completes. Use for async steps (pool hops inside the returned task).
  template <typename Fn>
  auto ThenAsync(Fn&& fn) const
      -> Future<typename AwaitValue<std::invoke_result_t<Fn, const T&>>::type> {
    using R = std::invoke_result_t<Fn, const T&>;
    using U = typename AwaitValue<R>::type;
    auto out = std::make_shared<detail::FutureState<U>>();
    detail::RunThenAsync<T, U>(RequireState(), out, std::forward<Fn>(fn));
    return Future<U>::FromState(out);
  }

  // Recover: fn(exception_ptr) -> T, used when this fails.
  template <typename Fn>
  Future<T> OnError(Fn&& fn) const {
    auto out = std::make_shared<detail::FutureState<T>>();
    detail::RunOnError<T>(RequireState(), out, std::forward<Fn>(fn));
    return Future<T>::FromState(out);
  }

  // Observe completion: fn(const std::optional<T>&, exception_ptr) is always
  // invoked (success or failure); the original result is propagated.
  template <typename Fn>
  Future<T> WhenComplete(Fn&& fn) const {
    auto out = std::make_shared<detail::FutureState<T>>();
    detail::RunWhenComplete<T>(RequireState(), out, std::forward<Fn>(fn));
    return Future<T>::FromState(out);
  }

  // ---- aggregates -------------------------------------------------------

  // All futures resolved -> vector of values (in order). First error wins.
  static Future<std::vector<T>> AllOf(const std::vector<Future<T>>& futures) {
    auto out = std::make_shared<detail::FutureState<std::vector<T>>>();
    if (futures.empty()) {
      out->ResolveValue(std::vector<T>{});
      return Future<std::vector<T>>::FromState(out);
    }
    auto result = std::make_shared<std::vector<T>>(futures.size());
    auto remaining = std::make_shared<std::atomic<std::size_t>>(futures.size());
    for (std::size_t i = 0; i < futures.size(); ++i) {
      detail::RunAllOf<T>(futures[i].RequireState(), out, result, remaining, i);
    }
    return Future<std::vector<T>>::FromState(out);
  }

  // First future to complete (success or failure) wins -> (index, value).
  static Future<std::pair<std::size_t, T>> AnyOf(
      const std::vector<Future<T>>& futures) {
    auto out = std::make_shared<detail::FutureState<std::pair<std::size_t, T>>>();
    auto winner = std::make_shared<std::atomic<bool>>(false);
    auto failed = std::make_shared<std::atomic<std::size_t>>(0);
    auto first_error = std::make_shared<std::exception_ptr>(nullptr);
    const std::size_t count = futures.size();
    for (std::size_t i = 0; i < count; ++i) {
      detail::RunAnyOf<T>(futures[i].RequireState(), out, winner, failed,
                          first_error, count, i);
    }
    return Future<std::pair<std::size_t, T>>::FromState(out);
  }

  // ---- await integration ------------------------------------------------

  // co_await future -> T (throws on error).
  auto operator co_await() const {
    struct Awaiter {
      std::shared_ptr<detail::FutureState<T>> state;
      coro::AsyncEvent::Awaiter event;
      bool await_ready() const noexcept { return event.await_ready(); }
      bool await_suspend(std::coroutine_handle<> awaiting) noexcept {
        return event.await_suspend(awaiting);
      }
      T await_resume() const { return state->Get(); }
    };
    auto s = RequireState();
    return Awaiter{s, coro::AsyncEvent::Awaiter{&s->done}};
  }

 private:
  explicit Future(std::shared_ptr<detail::FutureState<T>> state)
      : state_(std::move(state)) {}

 public:
  // (Advanced) wrap a shared completion state; used by the combinators to
  // build results whose value type differs from this future's.
  static Future FromState(std::shared_ptr<detail::FutureState<T>> state) {
    Future f;
    f.state_ = std::move(state);
    return f;
  }

 private:
  const std::shared_ptr<detail::FutureState<T>>& RequireState() const {
    if (!state_) throw std::logic_error("Future: invalid (default-constructed)");
    return state_;
  }

  std::shared_ptr<detail::FutureState<T>> state_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Future<void>
// ─────────────────────────────────────────────────────────────────────────────
template <>
class Future<void> {
 public:
  using ValueType = void;
  using Producer = std::function<Task<void>()>;

  Future() = default;
  Future(const Future&) = default;
  Future& operator=(const Future&) = default;

  bool valid() const { return state_ != nullptr; }

  static Future Just() {
    Future f;
    f.state_ = std::make_shared<detail::FutureState<void>>();
    f.state_->ResolveValue();
    return f;
  }
  static Future Error(std::exception_ptr error) {
    Future f;
    f.state_ = std::make_shared<detail::FutureState<void>>();
    f.state_->ResolveError(std::move(error));
    return f;
  }
  static Future Async(Producer producer, ThreadPool& pool) {
    Future f;
    f.state_ = std::make_shared<detail::FutureState<void>>();
    detail::RunProducer<void>(f.state_, std::move(producer), pool);
    return f;
  }

  // Run `work` on a dedicated thread (optionally pinned to a CPU with
  // affinity). Used by the compute tuner for heavy / CPU-bound tasks.
  static Future FromThread(std::function<void()> work, int cpu = -1) {
    Future f;
    f.state_ = std::make_shared<detail::FutureState<void>>();
    auto state = f.state_;
    std::thread([state, work = std::move(work), cpu]() {
      if (cpu >= 0) PinCurrentThread(cpu);
      try {
        work();
        state->ResolveValue();
      } catch (...) {
        state->ResolveError(std::current_exception());
      }
    }).detach();
    return f;
  }

  void Wait() const { RequireState()->Wait(); }
  bool WaitFor(std::chrono::milliseconds timeout) const {
    return RequireState()->WaitFor(timeout);
  }
  void Get() const { RequireState()->Get(); }
  void Get(std::chrono::milliseconds timeout) const {
    auto s = RequireState();
    if (!s->WaitFor(timeout)) throw std::runtime_error("Future::Get: timed out");
    s->Get();
  }

  // fn() -> U (sync), run when this completes successfully.
  template <typename Fn>
  auto Then(Fn&& fn) const -> Future<std::invoke_result_t<Fn>> {
    using U = std::invoke_result_t<Fn>;
    auto out = std::make_shared<detail::FutureState<U>>();
    detail::RunThenVoid<U>(RequireState(), out, std::forward<Fn>(fn));
    return Future<U>::FromState(out);
  }

  // fn() -> Future<U> (or Task<U>), awaited when this completes.
  template <typename Fn>
  auto ThenAsync(Fn&& fn) const -> Future<typename AwaitValue<std::invoke_result_t<Fn>>::type> {
    using R = std::invoke_result_t<Fn>;
    using U = typename AwaitValue<R>::type;
    auto out = std::make_shared<detail::FutureState<U>>();
    detail::RunThenAsyncVoid<U>(RequireState(), out, std::forward<Fn>(fn));
    return Future<U>::FromState(out);
  }

  // Recover: fn(exception_ptr) -> void, used when this fails.
  template <typename Fn>
  Future<void> OnError(Fn&& fn) const {
    auto out = std::make_shared<detail::FutureState<void>>();
    detail::RunOnErrorVoid(RequireState(), out, std::forward<Fn>(fn));
    return Future<void>::FromState(out);
  }

  // Observe completion: fn(exception_ptr) always invoked; result propagated.
  template <typename Fn>
  Future<void> WhenComplete(Fn&& fn) const {
    auto out = std::make_shared<detail::FutureState<void>>();
    detail::RunWhenCompleteVoid(RequireState(), out, std::forward<Fn>(fn));
    return Future<void>::FromState(out);
  }

  static Future<void> AllOf(const std::vector<Future<void>>& futures) {
    auto out = std::make_shared<detail::FutureState<void>>();
    if (futures.empty()) {
      out->ResolveValue();
      return Future<void>::FromState(out);
    }
    auto remaining = std::make_shared<std::atomic<std::size_t>>(futures.size());
    for (const auto& fut : futures) {
      detail::RunAllOfVoid(fut.RequireState(), out, remaining);
    }
    return Future<void>::FromState(out);
  }

  // First future to complete (success or failure) wins.
  static Future<void> AnyOf(const std::vector<Future<void>>& futures) {
    auto out = std::make_shared<detail::FutureState<void>>();
    auto winner = std::make_shared<std::atomic<bool>>(false);
    auto failed = std::make_shared<std::atomic<std::size_t>>(0);
    const std::size_t count = futures.size();
    for (const auto& fut : futures) {
      detail::RunAnyOfVoid(fut.RequireState(), out, winner, failed, count);
    }
    return Future<void>::FromState(out);
  }

  auto operator co_await() const {
    struct Awaiter {
      std::shared_ptr<detail::FutureState<void>> state;
      coro::AsyncEvent::Awaiter event;
      bool await_ready() const noexcept { return event.await_ready(); }
      bool await_suspend(std::coroutine_handle<> awaiting) noexcept {
        return event.await_suspend(awaiting);
      }
      void await_resume() const { state->Get(); }
    };
    auto s = RequireState();
    return Awaiter{s, coro::AsyncEvent::Awaiter{&s->done}};
  }

 private:
  explicit Future(std::shared_ptr<detail::FutureState<void>> state)
      : state_(std::move(state)) {}

 public:
  // (Advanced) wrap a shared completion state; used by the combinators.
  static Future FromState(std::shared_ptr<detail::FutureState<void>> state) {
    Future f;
    f.state_ = std::move(state);
    return f;
  }

 private:
  const std::shared_ptr<detail::FutureState<void>>& RequireState() const {
    if (!state_) throw std::logic_error("Future: invalid (default-constructed)");
    return state_;
  }

  std::shared_ptr<detail::FutureState<void>> state_;
};

}  // namespace lark::coro
