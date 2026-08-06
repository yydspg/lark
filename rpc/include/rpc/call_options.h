// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include "metric/metric.h"

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// RpcCallOptions: per-call transport knobs.
// ─────────────────────────────────────────────────────────────────────────────
struct RpcCallOptions {
  std::chrono::milliseconds timeout{1000};
  int retries = 0;
  // Transport metadata propagated to the server (deadlines, tracing ids, ...).
  std::unordered_map<std::string, std::string> metadata;
  // Optional unified monitor: the channel emits an "rpc.call" event when set.
  std::shared_ptr<::lark::metric::Monitor> monitor;

  RpcCallOptions() = default;
  explicit RpcCallOptions(std::chrono::milliseconds timeout) : timeout(timeout) {}
};

}  // namespace lark::rpc
