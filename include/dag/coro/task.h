#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace lark::coro {

using std::coroutine_handle;
using std::exception_ptr;
using std::forward;

template <typename T>
class Task;

namespace detail {

// Shared machinery for Task promises: symmetric-transfer continuation so that
// completing a task resumes its awaiter without growing the stack.
class TaskPromiseBase {
 public:
  struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<Promise> handle) noexcept {
      std::coroutine_handle<> continuation = handle.promise().continuation_;
      return continuation ? continuation : std::noop_coroutine();
    }

    void await_resume() const noexcept {}
  };

  std::suspend_always initial_suspend() noexcept { return {}; }
  FinalAwaiter final_suspend() noexcept { return {}; }

  void set_continuation(std::coroutine_handle<> continuation) noexcept {
    continuation_ = continuation;
  }

 private:
  std::coroutine_handle<> continuation_{};
};

template <typename T>
class TaskPromise final : public TaskPromiseBase {
 public:
  Task<T> get_return_object() noexcept;

  void unhandled_exception() noexcept { exception_ = std::current_exception(); }

  template <typename Value>
  void return_value(Value&& value) {
    value_.emplace(std::forward<Value>(value));
  }

  T& result() & {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
    return *value_;
  }

  T&& result() && {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
    return std::move(*value_);
  }

 private:
  std::optional<T> value_;
  std::exception_ptr exception_{};
};

template <>
class TaskPromise<void> final : public TaskPromiseBase {
 public:
  Task<void> get_return_object() noexcept;

  void unhandled_exception() noexcept { exception_ = std::current_exception(); }
  void return_void() noexcept {}

  void result() const {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }

 private:
  std::exception_ptr exception_{};
};

}  // namespace detail

// A lazily-started coroutine that yields a value of type T.
//
// The task begins running only when it is `co_await`-ed; awaiting resumes it
// via symmetric transfer and, on completion, resumes the awaiter. The task owns
// its coroutine frame and destroys it on destruction (RAII, no leaks).
template <typename T>
class [[nodiscard]] Task {
 public:
  using promise_type = detail::TaskPromise<T>;

  Task() noexcept = default;

  explicit Task(std::coroutine_handle<promise_type> handle) noexcept
      : handle_(handle) {}

  Task(Task&& other) noexcept
      : handle_(std::exchange(other.handle_, {})) {}

  Task& operator=(Task&& other) noexcept {
    if (this != &other) {
      Destroy();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  ~Task() { Destroy(); }

  bool valid() const noexcept { return static_cast<bool>(handle_); }

  auto operator co_await() && noexcept {
    struct Awaiter {
      std::coroutine_handle<promise_type> coro;

      bool await_ready() const noexcept { return !coro || coro.done(); }

      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<> awaiting) noexcept {
        coro.promise().set_continuation(awaiting);
        return coro;  // symmetric transfer -> start/resume the task
      }

      decltype(auto) await_resume() {
        return std::move(coro.promise()).result();
      }
    };
    return Awaiter{handle_};
  }

 private:
  void Destroy() noexcept {
    if (handle_) {
      handle_.destroy();
      handle_ = {};
    }
  }

  std::coroutine_handle<promise_type> handle_{};
};

namespace detail {

template <typename T>
Task<T> TaskPromise<T>::get_return_object() noexcept {
  return Task<T>{std::coroutine_handle<TaskPromise<T>>::from_promise(*this)};
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
  return Task<void>{
      std::coroutine_handle<TaskPromise<void>>::from_promise(*this)};
}

}  // namespace detail

}  // namespace lark::coro
