// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Flame-graph style profiling (Java flame-graph framework equivalent).
//
// Instrument hot paths with RAII scopes; the profiler aggregates every scope
// instance into a call tree with self / total time and entry counts, and can
// render it as a text flame graph or as collapsed stacks consumable by
// FlameGraph.pl-style tools. Great for load-testing / debug performance
// analysis.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "metric/metric.h"

namespace lark::metric::profile {

// ─────────────────────────────────────────────────────────────────────────────
// Frame: one node of the flame-graph call tree.
//   total_ns — inclusive time (self + all children)
//   self_ns  — exclusive time (total minus children)
//   count    — number of completed scope entries
// ─────────────────────────────────────────────────────────────────────────────
struct Frame {
  std::string name;
  int64_t self_ns = 0;
  int64_t total_ns = 0;
  int64_t count = 0;
  std::vector<Frame> children;  // sorted by total_ns (descending)
};

// ─────────────────────────────────────────────────────────────────────────────
// FlameGraphProfiler: thread-safe aggregation of instrumented scopes into a
// flame graph. Thread-local scope stacks build the full "a::b::c" call path;
// AddSample() records inclusive time per path.
// ─────────────────────────────────────────────────────────────────────────────
class FlameGraphProfiler {
 public:
  FlameGraphProfiler() = default;

  void Enable() { enabled_.store(true); }
  void Disable() { enabled_.store(false); }
  bool enabled() const { return enabled_.load(std::memory_order_relaxed); }

  // Record a completed scope instance (called internally by Scope).
  void AddSample(const std::string& path, int64_t elapsed_ns);

  // Aggregate call tree (deep copy).
  Frame Snapshot() const;
  // Text flame graph (indented, percentages of total time).
  std::string TextFlameGraph() const;
  // flamegraph.pl-compatible collapsed stacks ("leaf;...;root count").
  std::string CollapsedStacks() const;

  void Reset();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, int64_t> total_ns_;  // path -> inclusive ns
  std::unordered_map<std::string, int64_t> count_;     // path -> entries
  std::atomic<bool> enabled_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Scope: RAII timing scope. Pushes its name onto the thread-local path stack
// and records the inclusive elapsed time on destruction.
// ─────────────────────────────────────────────────────────────────────────────
class Scope {
 public:
  Scope(FlameGraphProfiler& profiler, std::string name);
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  FlameGraphProfiler& profiler_;
  std::string name_;
  int64_t start_ns_ = 0;
  bool active_ = false;
};

// Internal thread-local path-stack management (implemented in profile.cpp).
void PathStackPush(const std::string& name);
void PathStackPop();
std::string PathJoin();

// ─────────────────────────────────────────────────────────────────────────────
// Sampler: optional background thread that periodically snapshots the profiler
// and emits a "metric" "profile.sample" event into a Monitor. Use it to stream
// sampling info during load tests.
// ─────────────────────────────────────────────────────────────────────────────
class Sampler {
 public:
  Sampler(FlameGraphProfiler& profiler, std::chrono::milliseconds interval,
          std::shared_ptr<Monitor> monitor);
  ~Sampler();
  Sampler(const Sampler&) = delete;
  Sampler& operator=(const Sampler&) = delete;

  void Start();
  void Stop();
  bool running() const { return running_.load(); }

 private:
  void Loop();

  FlameGraphProfiler& profiler_;
  std::chrono::milliseconds interval_;
  std::shared_ptr<Monitor> monitor_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace lark::metric::profile

// PROFILE_SCOPE(profiler, name) — run a named timing scope until end of the
// enclosing block:
//   lark::metric::profile::FlameGraphProfiler prof;
//   prof.Enable();
//   void Compute() {
//     PROFILE_SCOPE(prof, "Compute");
//     // ... work ...
//   }
#define LARK_PROFILE_CONCAT2(a, b) a##b
#define LARK_PROFILE_CONCAT(a, b) LARK_PROFILE_CONCAT2(a, b)
#define PROFILE_SCOPE(profiler, name)                                         \
  ::lark::metric::profile::Scope LARK_PROFILE_CONCAT(lark_prof_, __LINE__)(   \
      (profiler), (name))
