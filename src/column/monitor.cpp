// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/monitor.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace lark::column::monitor {

void RunStats::reset() {
  feed_elapsed = compute_elapsed = fetch_elapsed = nanoseconds::zero();
  compute_workers = 0;
  ops.clear();
  modules.clear();
  pressure.clear();
}

namespace {

std::string FmtNs(nanoseconds ns) {
  const double us = std::chrono::duration_cast<std::chrono::microseconds>(ns)
                        .count() /
                    1000.0;
  std::ostringstream os;
  os << std::fixed;
  if (us >= 1000.0) {
    os.precision(2);
    os << (us / 1000.0) << "ms";
  } else {
    os.precision(2);
    os << us << "us";
  }
  return os.str();
}

std::string FmtPct(double v) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(1) << (v * 100.0) << "%";
  return os.str();
}

}  // namespace

std::string RunStats::summary() const {
  std::ostringstream os;

  os << "--- run stats ---\n";
  os << "  feed   : " << FmtNs(feed_elapsed) << "\n";
  os << "  compute: " << FmtNs(compute_elapsed) << " (workers="
     << compute_workers << ")\n";
  os << "  fetch  : " << FmtNs(fetch_elapsed) << "\n";

  if (!modules.empty()) {
    std::vector<ModuleTiming> sorted = modules;
    std::sort(sorted.begin(), sorted.end(),
              [](const ModuleTiming& a, const ModuleTiming& b) {
                return a.elapsed > b.elapsed;
              });
    os << "  modules (" << modules.size() << "):\n";
    for (const auto& m : sorted) {
      os << "    " << m.module << ": " << FmtNs(m.elapsed) << " ("
         << m.node_count << " nodes)\n";
    }
  }

  if (!pressure.empty()) {
    os << "  cpu pressure:\n";
    for (const auto& p : pressure) {
      os << "    " << p.pool << ": util=" << FmtPct(p.utilization)
         << " busy=" << FmtNs(p.busy) << " wall=" << FmtNs(p.wall)
         << " workers=" << p.workers << "\n";
    }
  }

  if (!ops.empty()) {
    std::vector<OpTiming> sorted = ops;
    std::sort(sorted.begin(), sorted.end(),
              [](const OpTiming& a, const OpTiming& b) {
                return a.elapsed > b.elapsed;
              });
    os << "  slowest ops:\n";
    const size_t k = std::min<size_t>(sorted.size(), 8);
    for (size_t i = 0; i < k; ++i) {
      os << "    [" << sorted[i].module << "] " << sorted[i].node_id << " ("
         << sorted[i].op_type << "): " << FmtNs(sorted[i].elapsed) << "\n";
    }
  }
  return os.str();
}

std::string StatsCollector::ModuleOf(const std::string& node_id) {
  const size_t slash = node_id.find('/');
  return slash == std::string::npos ? node_id : node_id.substr(0, slash);
}

void StatsCollector::OnFeedEnd(nanoseconds elapsed) {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.feed_elapsed = elapsed;
}

void StatsCollector::OnComputeEnd(nanoseconds elapsed) {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.compute_elapsed = elapsed;
}

void StatsCollector::OnFetchEnd(nanoseconds elapsed) {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.fetch_elapsed = elapsed;
}

void StatsCollector::OnNodeEnd(const std::string& node_id,
                               const std::string& op_type,
                               nanoseconds elapsed) {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.ops.push_back(
      OpTiming{node_id, op_type, ModuleOf(node_id), elapsed});
}

void StatsCollector::OnNodeError(const std::string& node_id,
                                 const std::string& op_type,
                                 const exception_ptr&) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Record failed ops with a zero-duration entry so they are visible in
  // the op timeline; the exception itself is surfaced by the pipeline.
  stats_.ops.push_back(
      OpTiming{node_id, op_type, ModuleOf(node_id), nanoseconds::zero()});
}

void StatsCollector::OnModuleEnd(const std::string& module,
                                 nanoseconds elapsed,
                                 std::size_t node_count) {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.modules.push_back(ModuleTiming{module, elapsed, node_count});
}

void StatsCollector::OnCpuPressure(const std::string& pool, double utilization,
                                   nanoseconds busy, nanoseconds wall,
                                   std::size_t workers) {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.compute_workers = workers;
  stats_.pressure.push_back(CpuPressure{pool, utilization, busy, wall, workers});
}

}  // namespace lark::column::monitor
