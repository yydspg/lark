#pragma once

#include <coroutine>
#include <exception>

namespace lark::coro {

using std::coroutine_handle;

// A detached, self-owning coroutine used to spawn node executions.
//
// initial_suspend is `suspend_never`, so the body runs immediately on the
// spawning thread up to its first real suspension point (typically
// `co_await pool.Schedule()`), then returns control to the spawner.
// final_suspend is `suspend_never`, so the frame destroys itself on completion
// -> no manual cleanup, no leaks. The coroutine body must be noexcept in
// practice (it catches its own exceptions); an escaping exception terminates.
struct FireAndForget {
  struct promise_type {
    FireAndForget get_return_object() const noexcept { return {}; }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    void return_void() const noexcept {}
    void unhandled_exception() const noexcept { std::terminate(); }
  };
};

}  // namespace lark::coro
