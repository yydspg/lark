// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include "metric/metric.h"

namespace lark::column::monitor {

using Clock = std::chrono::steady_clock;
using std::chrono::nanoseconds;

// ─────────────────────────────────────────────────────────────────────────────
// Timing records (the column-engine domain stats model).
// ─────────────────────────────────────────────────────────────────────────────

struct OpTiming {
  std::string node_id;
  std::string op_type;
  std::string module;  // derived from the node id prefix
  nanoseconds elapsed{0};
};

struct ModuleTiming {
  std::string module;
  nanoseconds elapsed{0};
  std::size_t node_count = 0;
};

struct CpuPressure {
  std::string pool;  // e.g. "compute"
  double utilization = 0.0;  // busy / (wall * workers)
  nanoseconds busy{0};
  nanoseconds wall{0};
  std::size_t workers = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// RunStats: aggregate snapshot of a single run (feed + compute + fetch).
// ─────────────────────────────────────────────────────────────────────────────
struct RunStats {
  nanoseconds feed_elapsed{0};
  nanoseconds compute_elapsed{0};
  nanoseconds fetch_elapsed{0};
  std::size_t compute_workers = 0;

  std::vector<OpTiming> ops;
  std::vector<ModuleTiming> modules;
  std::vector<CpuPressure> pressure;

  void reset();

  // Human-readable report.
  std::string summary() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// StatsCollector: the column-engine monitoring implementation.
//
// It is an implementation of the unified lark::metric::Monitor abstraction.
// The framework (Pipeline / ComputeGraph / ExecutionContext) emits
// lark::metric::Event records; this collector turns the "column.*" events
// back into the domain RunStats model. Business code may attach any other
// monitor implementation instead (e.g. a logging monitor in dev).
// ─────────────────────────────────────────────────────────────────────────────
class StatsCollector : public ::lark::metric::Monitor {
 public:
  StatsCollector() = default;

  void Emit(const ::lark::metric::Event& event) override;

  RunStats& stats() noexcept { return stats_; }
  const RunStats& stats() const noexcept { return stats_; }

 private:
  void HandlePhase(const ::lark::metric::Event& event);
  void HandleNode(const ::lark::metric::Event& event);
  void HandleModule(const ::lark::metric::Event& event);
  void HandleCpuPressure(const ::lark::metric::Event& event);

  // Derive the module name from a node id ("<module>/...") — falls back to the
  // full id when there is no separator.
  static std::string ModuleOf(const std::string& node_id);

  mutable std::mutex mutex_;
  RunStats stats_;
};

}  // namespace lark::column::monitor
