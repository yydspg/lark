// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Result<T, E> — a small, dependency-free alternative to std::expected for
// fallible operations.

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace lark::toolkit {

// ─────────────────────────────────────────────────────────────────────────────
// Result<T, E>: either a value (Ok) or an error (Err).
//
//   toolkit::Result<int> r = toolkit::Result<int>::Ok(42);
//   if (r) use(r.value());
//   auto v = toolkit::Result<int>::Err("boom");
//   if (!v) handle(v.error());
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, typename E = std::string>
class Result {
 public:
  Result(Result&&) noexcept = default;
  Result& operator=(Result&&) noexcept = default;
  Result(const Result&) = default;
  Result& operator=(const Result&) = default;

  static Result Ok(T value) {
    Result r;
    r.value_ = std::move(value);
    return r;
  }
  static Result Err(E error) {
    Result r;
    r.error_ = std::move(error);
    return r;
  }

  bool ok() const noexcept { return value_.has_value(); }
  explicit operator bool() const noexcept { return ok(); }

  T& value() {
    if (!value_) throw std::runtime_error(ErrorText());
    return *value_;
  }
  const T& value() const {
    if (!value_) throw std::runtime_error(ErrorText());
    return *value_;
  }

  T value_or(T fallback) const { return value_.value_or(std::move(fallback)); }

  const E& error() const {
    if (!error_) throw std::logic_error("Result::error: no error present");
    return *error_;
  }

 private:
  Result() = default;
  std::string ErrorText() const { return error_ ? *error_ : "Result has no value"; }

  std::optional<T> value_;
  std::optional<E> error_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Result<void, E>: signals success/failure without a value.
// ─────────────────────────────────────────────────────────────────────────────
template <typename E>
class Result<void, E> {
 public:
  Result() = default;
  Result(Result&&) noexcept = default;
  Result& operator=(Result&&) noexcept = default;
  Result(const Result&) = default;
  Result& operator=(const Result&) = default;

  static Result Ok() { return Result{}; }
  static Result Err(E error) {
    Result r;
    r.error_ = std::move(error);
    return r;
  }

  bool ok() const noexcept { return !error_.has_value(); }
  explicit operator bool() const noexcept { return ok(); }
  const E& error() const {
    if (!error_) throw std::logic_error("Result::error: no error present");
    return *error_;
  }

 private:
  std::optional<E> error_;
};

}  // namespace lark::toolkit
