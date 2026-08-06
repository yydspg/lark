// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/monitor.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
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

// ---- event handling --------------------------------------------------------

void StatsCollector::HandlePhase(const ::lark::monitor::Event& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (event.action == "feed.end")
    stats_.feed_elapsed = event.duration;
  else if (event.action == "compute.end")
    stats_.compute_elapsed = event.duration;
  else if (event.action == "fetch.end")
    stats_.fetch_elapsed = event.duration;
}

void StatsCollector::HandleNode(const ::lark::monitor::Event& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string* op = event.Get("op");
  stats_.ops.push_back(
      OpTiming{event.subject, op ? *op : "", ModuleOf(event.subject),
               event.duration});
}

void StatsCollector::HandleModule(const ::lark::monitor::Event& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string* count = event.Get("node_count");
  stats_.modules.push_back(
      ModuleTiming{event.subject, event.duration,
                   count ? static_cast<std::size_t>(std::stoull(*count)) : 0});
}

void StatsCollector::HandleCpuPressure(const ::lark::monitor::Event& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string* pool = event.Get("pool");
  const std::string* busy = event.Get("busy_ns");
  const std::string* wall = event.Get("wall_ns");
  const std::string* workers = event.Get("workers");
  const std::string* util = event.Get("utilization");
  CpuPressure p;
  p.pool = pool ? *pool : "compute";
  p.busy = busy ? nanoseconds(std::stoll(*busy)) : nanoseconds::zero();
  p.wall = wall ? nanoseconds(std::stoll(*wall)) : nanoseconds::zero();
  p.workers = workers ? static_cast<std::size_t>(std::stoull(*workers)) : 0;
  p.utilization = util ? std::stod(*util) : 0.0;
  stats_.compute_workers = p.workers;
  stats_.pressure.push_back(std::move(p));
}

void StatsCollector::Emit(const ::lark::monitor::Event& event) {
  if (event.source != "column") return;
  if (event.action == "feed.end" || event.action == "compute.end" ||
      event.action == "fetch.end") {
    HandlePhase(event);
  } else if (event.action == "node.end" || event.action == "node.error") {
    HandleNode(event);
  } else if (event.action == "module.end") {
    HandleModule(event);
  } else if (event.action == "cpu.pressure") {
    HandleCpuPressure(event);
  }
}

}  // namespace lark::column::monitor
