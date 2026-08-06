#include "coro/async_event.h"

namespace lark::coro {

AsyncEvent::AsyncEvent() noexcept : state_(nullptr) {}

bool AsyncEvent::IsSet() const noexcept {
  return state_.load(std::memory_order_acquire) == this;
}

void AsyncEvent::Set() noexcept {
  void* old = state_.exchange(this, std::memory_order_acq_rel);
  if (old == this) {
    return;  // already set
  }
  auto* waiter = static_cast<Awaiter*>(old);
  while (waiter != nullptr) {
    // Save `next` before resume(): resuming may run the awaiting coroutine
    // to completion and destroy this Awaiter (it lives in that frame).
    auto* next = waiter->next;
    waiter->handle.resume();
    waiter = next;
  }
}

void AsyncEvent::Reset() noexcept {
  void* expected = this;
  state_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel,
                                 std::memory_order_relaxed);
}

AsyncEvent::Awaiter::Awaiter(AsyncEvent* event) noexcept : event_(event) {}

bool AsyncEvent::Awaiter::await_ready() const noexcept {
  return event_->IsSet();
}

bool AsyncEvent::Awaiter::await_suspend(coroutine_handle<> awaiting) noexcept {
  handle = awaiting;
  void* old = event_->state_.load(std::memory_order_acquire);
  do {
    if (old == event_) {
      return false;  // became set while suspending -> resume immediately
    }
    next = static_cast<Awaiter*>(old);
  } while (!event_->state_.compare_exchange_weak(
      old, this, std::memory_order_acq_rel, std::memory_order_acquire));
  return true;
}

void AsyncEvent::Awaiter::await_resume() const noexcept {}

AsyncEvent::Awaiter AsyncEvent::operator co_await() noexcept {
  return Awaiter{this};
}

}  // namespace lark::coro
