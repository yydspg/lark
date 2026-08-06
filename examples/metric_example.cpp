// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// metric observability usage example: flame-graph profiling + probes.
//
// During a load test / debug run, instrument hot paths with PROFILE_SCOPE and
// render a flame graph; use probes to catch null pointers / invariants /
// statistical anomalies early.

#include <chrono>
#include <iostream>
#include <thread>

#include "metric/metric.h"

using namespace lark::metric;
using namespace std::chrono_literals;

lark::metric::profile::FlameGraphProfiler g_prof;

void ProcessRow(int id) {
  PROFILE_SCOPE(g_prof, "process");
  std::this_thread::sleep_for(id % 3 == 0 ? 200us : 100us);
}

void ComputeBatch(int n) {
  PROFILE_SCOPE(g_prof, "batch");
  for (int i = 0; i < n; ++i) {
    PROFILE_SCOPE(g_prof, "row");
    ProcessRow(i);
  }
}

int main() {
  // 1) Flame-graph profiling.
  auto stats = std::make_shared<StatsMonitor>();
  g_prof.Enable();

  // Optional sampler: stream periodic profile.sample events into a monitor.
  profile::Sampler sampler(g_prof, 10ms, stats);
  sampler.Start();

  for (int b = 0; b < 3; ++b) ComputeBatch(20);  // simulated load

  sampler.Stop();
  g_prof.Disable();

  std::cout << g_prof.TextFlameGraph();   // text flame graph
  std::cout << "\n" << g_prof.CollapsedStacks();  // flamegraph.pl format

  // 2) Probes: null pointers / invariants / anomalies.
  int* maybe_null = nullptr;
  if (!probe::NotNull(maybe_null, "maybe_null", stats)) {
    std::cout << "\nnull probe fired: maybe_null\n";
  }

  probe::AnomalyDetector latency(stats, 3.0);
  for (double ms : {1.0, 1.1, 0.9, 1.2, 1.05, 0.95, 1.1, 50.0}) {
    if (latency.Feed(ms, "latency")) {
      std::cout << "latency anomaly: " << ms << "ms\n";
    }
  }

  std::cout << "\nprofile.sample events: " << stats->event_count() << "\n";
  return 0;
}
