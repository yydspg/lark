// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "metric/probe.h"

namespace lark::metric::probe {

bool AnomalyDetector::Feed(double value, const std::string& subject) {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool anomaly = [&] {
    if (n_ < 1) return false;
    const double sd = std::sqrt(m2_ / static_cast<double>(n_));
    return sd > 0.0 && std::fabs(value - mean_) > k_ * sd;
  }();

  if (anomaly && monitor_) {
    Event e{"metric", "probe.anomaly", subject};
    e.attr("value", std::to_string(value));
    e.attr("mean", std::to_string(mean_));
    e.attr("stddev", std::to_string(std::sqrt(m2_ / n_)));
    e.attr("k", std::to_string(k_));
    e.ok = false;
    monitor_->Emit(e);
  }

  // Welford online update.
  ++n_;
  const double delta = value - mean_;
  mean_ += delta / static_cast<double>(n_);
  m2_ += delta * (value - mean_);
  return anomaly;
}

double AnomalyDetector::mean() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mean_;
}

double AnomalyDetector::stddev() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return n_ < 1 ? 0.0 : std::sqrt(m2_ / static_cast<double>(n_));
}

std::size_t AnomalyDetector::count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return n_;
}

}  // namespace lark::metric::probe
