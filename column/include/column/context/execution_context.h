// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>

#include "column/monitor.h"
#include "column/tensor_store.h"
#include "column/tensor_table.h"
#include "metric/metric.h"

namespace lark::column::context {

// ─────────────────────────────────────────────────────────────────────────────
// RunPhase: the three stages of one pipeline execution.
//
//   kFeed    — business rows are converted to columns and seeded into the
//              store (行转列).
//   kCompute — the global compute graph (module sub-graphs wired together by
//              the framework) runs; every intermediate is stored in the
//              execution store.
//   kFetch   — result columns are materialized back to rows (列转行).
//
// The context tracks the active phase and accumulates per-phase timing so
// monitoring can attribute time to feed / compute / fetch independently.
// ─────────────────────────────────────────────────────────────────────────────
enum class RunPhase { kFeed = 0, kCompute = 1, kFetch = 2 };

inline const char* phase_name(RunPhase p) noexcept {
  switch (p) {
    case RunPhase::kFeed:
      return "feed";
    case RunPhase::kCompute:
      return "compute";
    case RunPhase::kFetch:
      return "fetch";
  }
  return "unknown";
}

using std::chrono::nanoseconds;

// ─────────────────────────────────────────────────────────────────────────────
// ExecutionContext: the per-run data bag shared by all compute nodes.
//
//   * feed  — TensorTable with the raw fed columns (row-shaped input data is
//             converted to columns by the pipeline).
//   * store — TensorStore holding every intermediate produced by ops.
//   * fetch — TensorTable with the materialized result columns.
//
// The store deliberately has no uniform-row-count invariant so ops such as
// filter / reduce can store different-length tensors.
// ─────────────────────────────────────────────────────────────────────────────
class ExecutionContext {
 public:
  ExecutionContext() = default;
  ExecutionContext(const ExecutionContext&) = delete;
  ExecutionContext& operator=(const ExecutionContext&) = delete;

  // ---- phase management -------------------------------------------------
  RunPhase phase() const noexcept { return phase_; }
  const char* phase_name() const noexcept {
    return ::lark::column::context::phase_name(phase_);
  }

  // Enter a phase; if another phase was active it is finalized first.
  void begin_phase(RunPhase p);
  // Finalize the active phase (stop its timer).
  void end_phase();
  nanoseconds phase_elapsed(RunPhase p) const noexcept {
    return phase_elapsed_[static_cast<std::size_t>(p)];
  }

  // ---- feed partition ---------------------------------------------------
  void set_feed(TensorTable table);
  const TensorTable& feed() const noexcept { return feed_; }

  // ---- fetch partition --------------------------------------------------
  void set_fetch(TensorTable table);
  const TensorTable& fetch() const noexcept { return fetch_; }

  // ---- intermediate storage ---------------------------------------------
  TensorStore& store() noexcept { return store_; }
  const TensorStore& store() const noexcept { return store_; }
  void clear_store() { store_.clear(); }

  // ---- monitoring -------------------------------------------------------
  // Any unified monitor implementation (lark::metric::Monitor); the framework
  // emits "column.*" events into it. Default: none (no-op).
  void set_monitor(std::shared_ptr<::lark::metric::Monitor> monitor);
  ::lark::metric::Monitor* monitor() const noexcept { return monitor_.get(); }
  bool has_monitor() const noexcept { return monitor_ != nullptr; }

 private:
  RunPhase phase_ = RunPhase::kFeed;
  nanoseconds phase_elapsed_[3]{};
  std::chrono::steady_clock::time_point phase_start_{};
  bool phase_active_ = false;

  TensorTable feed_;
  TensorTable fetch_;
  TensorStore store_;
  std::shared_ptr<::lark::metric::Monitor> monitor_;
};

}  // namespace lark::column::context
