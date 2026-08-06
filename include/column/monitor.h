// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

namespace lark::column::monitor {

using Clock = std::chrono::steady_clock;
using std::chrono::nanoseconds;
using std::exception_ptr;

// ─────────────────────────────────────────────────────────────────────────────
// Timing records
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
// ExecutionMonitor: the generic observability interface of the engine.
//
// The framework (Pipeline / ComputeGraph executor / ExecutionContext) invokes
// these callbacks around every stage. Business code plugs in its own
// implementation to export metrics / tracing / logging. Callbacks may run
// concurrently from multiple worker threads, so implementations must be
// thread-safe.
// ─────────────────────────────────────────────────────────────────────────────
class ExecutionMonitor {
 public:
  virtual ~ExecutionMonitor() = default;

  // ---- phases (feed → compute → fetch) -------------------------------
  virtual void OnFeedStart() {}
  virtual void OnFeedEnd(nanoseconds /*elapsed*/) {}
  virtual void OnComputeStart() {}
  virtual void OnComputeEnd(nanoseconds /*elapsed*/) {}
  virtual void OnFetchStart() {}
  virtual void OnFetchEnd(nanoseconds /*elapsed*/) {}

  // ---- per-node execution ---------------------------------------------
  virtual void OnNodeStart(const std::string& /*node_id*/,
                           const std::string& /*op_type*/) {}
  virtual void OnNodeEnd(const std::string& /*node_id*/,
                         const std::string& /*op_type*/,
                         nanoseconds /*elapsed*/) {}
  virtual void OnNodeError(const std::string& /*node_id*/,
                           const std::string& /*op_type*/,
                           const exception_ptr& /*error*/) {}

  // ---- per-module aggregate (reported once per run, after compute) ----
  virtual void OnModuleEnd(const std::string& /*module*/,
                           nanoseconds /*elapsed*/, std::size_t /*node_count*/) {}

  // ---- CPU pressure distribution ---------------------------------------
  virtual void OnCpuPressure(const std::string& /*pool*/, double /*utilization*/,
                             nanoseconds /*busy*/, nanoseconds /*wall*/,
                             std::size_t /*workers*/) {}
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

  // Human-readable report (uses << operators; no external deps).
  std::string summary() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// StatsCollector: a thread-safe default monitor that accumulates a RunStats.
// This is the "通用监控接口" out-of-the-box implementation.
// ─────────────────────────────────────────────────────────────────────────────
class StatsCollector : public ExecutionMonitor {
 public:
  StatsCollector() = default;

  RunStats& stats() noexcept { return stats_; }
  const RunStats& stats() const noexcept { return stats_; }

  void OnFeedEnd(nanoseconds elapsed) override;
  void OnComputeEnd(nanoseconds elapsed) override;
  void OnFetchEnd(nanoseconds elapsed) override;
  void OnNodeEnd(const std::string& node_id, const std::string& op_type,
                 nanoseconds elapsed) override;
  void OnNodeError(const std::string& node_id, const std::string& op_type,
                   const exception_ptr&) override;
  void OnModuleEnd(const std::string& module, nanoseconds elapsed,
                   std::size_t node_count) override;
  void OnCpuPressure(const std::string& pool, double utilization,
                     nanoseconds busy, nanoseconds wall,
                     std::size_t workers) override;

 private:
  // Derive the module name from a node id ("<module>/...") — falls back to the
  // full id when there is no separator.
  static std::string ModuleOf(const std::string& node_id);

  mutable std::mutex mutex_;
  RunStats stats_;
};

}  // namespace lark::column::monitor
