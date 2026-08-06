// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Time / duration helpers.

#include <chrono>
#include <cstdint>
#include <string>

namespace lark::toolkit::time {

using Clock = std::chrono::steady_clock;

// Monotonic now (nanoseconds since an arbitrary epoch).
inline int64_t NowNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

// Human-readable duration: "120ns" / "12.3us" / "4.5ms" / "2.1s".
std::string FormatDuration(std::chrono::nanoseconds d);
inline std::string FormatDurationNs(int64_t ns) {
  return FormatDuration(std::chrono::nanoseconds(ns));
}

}  // namespace lark::toolkit::time
