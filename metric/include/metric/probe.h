// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Null-pointer / invariant / anomaly probes.
//
// Detect the kind of runtime problems (null derefs, broken invariants,
// statistical outliers) early during debugging / load tests and surface them
// through the unified metric::Monitor as "metric" "probe.*" events.

#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "metric/metric.h"

namespace lark::metric::probe {

// ─────────────────────────────────────────────────────────────────────────────
// Null-pointer probes
// ─────────────────────────────────────────────────────────────────────────────

// Non-throwing null check. On null, emits a "probe.null" event and returns
// false.
inline bool NotNull(const void* p, const std::string& what,
                    std::shared_ptr<Monitor> monitor = {}) {
  if (p) return true;
  if (monitor) {
    Event e{"metric", "probe.null", what};
    e.ok = false;
    monitor->Emit(e);
  }
  return false;
}

// Throwing null check: returns a reference to *p or throws runtime_error
// (after emitting a "probe.null" event when a monitor is attached).
template <typename T>
T& CheckNotNull(T* p, const std::string& what,
                std::shared_ptr<Monitor> monitor = {}) {
  if (p) return *p;
  if (monitor) {
    Event e{"metric", "probe.null", what};
    e.ok = false;
    monitor->Emit(e);
  }
  throw std::runtime_error("probe: null pointer '" + what + "'");
}

// ─────────────────────────────────────────────────────────────────────────────
// Invariant check
// ─────────────────────────────────────────────────────────────────────────────
// When `ok` is false: emits a "probe.fail" event and (optionally) throws.
// Returns `ok`.
inline bool Check(bool ok, const std::string& what,
                  std::shared_ptr<Monitor> monitor = {},
                  bool throw_on_fail = false) {
  if (ok) return true;
  if (monitor) {
    Event e{"metric", "probe.fail", what};
    e.ok = false;
    monitor->Emit(e);
  }
  if (throw_on_fail) {
    throw std::runtime_error("probe: invariant failed '" + what + "'");
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// AnomalyDetector: online (Welford) mean/stddev; values farther than k*sigma
// from the running mean are flagged and emitted as "probe.anomaly" events.
// Thread-safe.
// ─────────────────────────────────────────────────────────────────────────────
class AnomalyDetector {
 public:
  explicit AnomalyDetector(std::shared_ptr<Monitor> monitor = {},
                           double k = 3.0)
      : monitor_(std::move(monitor)), k_(k) {}

  // Feed a value; returns true when it is an anomaly (and emits an event).
  bool Feed(double value, const std::string& subject = "");

  double mean() const;
  double stddev() const;  // population standard deviation
  std::size_t count() const;

 private:
  std::shared_ptr<Monitor> monitor_;
  double k_;
  mutable std::mutex mutex_;
  double mean_ = 0.0;
  double m2_ = 0.0;  // sum of squared differences from the current mean
  std::size_t n_ = 0;
};

}  // namespace lark::metric::probe
