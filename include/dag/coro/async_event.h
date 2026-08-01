#pragma once

#include <atomic>
#include <coroutine>

namespace lark::coro {

using std::coroutine_handle;

// A one-shot, thread-safe awaitable event with multiple awaiters.
//
// State machine of the atomic `state_` pointer:
//   nullptr   -> not set, no waiters
//   this      -> set (resolved)
//   otherwise -> head of an intrusive singly-linked list of Awaiters
//
// Used as a node-completion signal: downstream node coroutines `co_await` the
// event and are resumed (on the thread that calls Set()) once it fires.
class AsyncEvent {
 public:
  AsyncEvent() noexcept;

  AsyncEvent(const AsyncEvent&) = delete;
  AsyncEvent& operator=(const AsyncEvent&) = delete;

  bool IsSet() const noexcept;

  // Resolve the event and resume every waiting coroutine. Idempotent.
  void Set() noexcept;

  // Return the event to the unset state (only meaningful when not concurrently
  // awaited); enables reuse of a graph across requests.
  void Reset() noexcept;

  class Awaiter {
   public:
    explicit Awaiter(AsyncEvent* event) noexcept;

    bool await_ready() const noexcept;
    bool await_suspend(coroutine_handle<> awaiting) noexcept;
    void await_resume() const noexcept;

    coroutine_handle<> handle{};
    Awaiter* next = nullptr;

   private:
    AsyncEvent* event_;
  };

  Awaiter operator co_await() noexcept;

 private:
  std::atomic<void*> state_;
};

}  // namespace lark::coro
