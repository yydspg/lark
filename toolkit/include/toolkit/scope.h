// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// RAII scope guard — run a cleanup callable on scope exit.

#include <functional>
#include <utility>

namespace lark::toolkit {

// Runs its callable when destroyed unless Release() was called.
class ScopeGuard {
 public:
  template <typename Fn>
  explicit ScopeGuard(Fn&& fn) : fn_(std::forward<Fn>(fn)) {}

  ~ScopeGuard() {
    if (armed_ && fn_) fn_();
  }

  ScopeGuard(ScopeGuard&& other) noexcept
      : fn_(std::move(other.fn_)), armed_(other.armed_) {
    other.armed_ = false;
  }
  ScopeGuard& operator=(ScopeGuard&&) = delete;
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;

  void Release() noexcept { armed_ = false; }

 private:
  std::function<void()> fn_;
  bool armed_ = true;
};

template <typename Fn>
ScopeGuard MakeScopeGuard(Fn&& fn) {
  return ScopeGuard(std::forward<Fn>(fn));
}

}  // namespace lark::toolkit

// LARK_DEFER(cleanup_statement); — run at the end of the enclosing scope.
//   auto fd = Open(...);
//   LARK_DEFER(Close(fd););
#define LARK_CONCAT_IMPL(a, b) a##b
#define LARK_CONCAT(a, b) LARK_CONCAT_IMPL(a, b)
#define LARK_DEFER(...)                                                       \
  auto LARK_CONCAT(lark_defer_, __LINE__) =                                   \
      ::lark::toolkit::MakeScopeGuard([&] { __VA_ARGS__; })
