// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for the metric observability features: flame-graph profiling
// (profile.h) and null-pointer / anomaly probes (probe.h).

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "metric/metric.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool cond, const char* expr, const char* file, int line) {
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::cerr << "FAIL: " << file << ":" << line << ": " << expr << "\n";
  }
}
#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)

void ExpectThrow(std::function<void()> fn, const char* what, const char* file,
                 int line) {
  ++g_checks;
  bool threw = false;
  try {
    fn();
  } catch (const std::exception&) {
    threw = true;
  }
  if (!threw) {
    ++g_failures;
    std::cerr << "FAIL: " << file << ":" << line << ": expected throw from "
              << what << "\n";
  }
}
#define CHECK_THROWS(fn) ExpectThrow((fn), #fn, __FILE__, __LINE__)

using namespace std::chrono_literals;
using namespace lark::metric;
using lark::metric::profile::Frame;

// ─────────────────────────────────────────────────────────────────────────────
// Flame-graph profiling
// ─────────────────────────────────────────────────────────────────────────────
void RunWorkload(profile::FlameGraphProfiler& prof, int loops) {
  for (int i = 0; i < loops; ++i) {
    PROFILE_SCOPE(prof, "outer");
    {
      PROFILE_SCOPE(prof, "innerA");
      std::this_thread::sleep_for(1ms);
    }
    {
      PROFILE_SCOPE(prof, "innerB");
      std::this_thread::sleep_for(2ms);
    }
    PROFILE_SCOPE(prof, "parallel");
    std::this_thread::sleep_for(3ms);
  }
}

void TestFlameGraph() {
  std::cout << "Test flame-graph profiling...\n";

  profile::FlameGraphProfiler prof;
  CHECK(!prof.enabled());

  // disabled -> scopes are no-ops
  RunWorkload(prof, 2);
  CHECK(prof.Snapshot().children.empty());

  prof.Enable();
  CHECK(prof.enabled());

  const int loops = 5;
  RunWorkload(prof, loops);
  prof.Disable();

  Frame root = prof.Snapshot();
  CHECK(root.children.size() == 1);         // everything under "outer"
  CHECK(root.children[0].name == "outer");
  const Frame* outer = &root.children[0];

  CHECK(outer->count == loops);
  CHECK(outer->children.size() == 3);       // innerA + innerB + parallel
  const Frame* innerA = nullptr;
  const Frame* innerB = nullptr;
  const Frame* parallel = nullptr;
  for (const auto& c : outer->children) {
    if (c.name == "innerA") innerA = &c;
    if (c.name == "innerB") innerB = &c;
    if (c.name == "parallel") parallel = &c;
  }
  CHECK(innerA != nullptr && innerB != nullptr && parallel != nullptr);
  CHECK(innerA->count == loops);
  CHECK(parallel->count == loops);
  CHECK(innerA->total_ns >= loops * 1000000);        // >= 1ms/loop
  CHECK(innerB->total_ns >= 2 * loops * 1000000);    // >= 2ms/loop
  CHECK(parallel->total_ns >= 3 * loops * 1000000);  // >= 3ms/loop
  CHECK(outer->total_ns >= 6 * loops * 1000000);     // >= 6ms/loop inclusive

  // self + children totals must not exceed the outer total
  int64_t kids = 0;
  for (const auto& c : outer->children) kids += c.total_ns;
  CHECK(outer->total_ns >= kids);
  CHECK(outer->self_ns >= 0);

  // text flame graph + collapsed stacks
  const std::string text = prof.TextFlameGraph();
  CHECK(text.find("outer") != std::string::npos);
  CHECK(text.find("innerA") != std::string::npos);
  const std::string collapsed = prof.CollapsedStacks();
  CHECK(collapsed.find("innerA;outer") != std::string::npos);  // leaf;root
  CHECK(collapsed.find("parallel;outer") != std::string::npos);

  prof.Reset();
  CHECK(prof.Snapshot().children.empty());

  std::cout << "  done\n";
}

void TestSampler() {
  std::cout << "Test sampler...\n";

  profile::FlameGraphProfiler prof;
  prof.Enable();
  auto stats = std::make_shared<StatsMonitor>();

  profile::Sampler sampler(prof, 5ms, stats);
  sampler.Start();
  RunWorkload(prof, 8);  // ~ 8 * 6ms
  sampler.Stop();

  prof.Disable();
  CHECK(sampler.running() == false);
  CHECK(stats->event_count() >= 1);
  CHECK(stats->duration("profile.sample").count() >= 0);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Probes
// ─────────────────────────────────────────────────────────────────────────────
void TestNullProbe() {
  std::cout << "Test null-pointer probes...\n";

  auto stats = std::make_shared<StatsMonitor>();

  int* p = nullptr;
  CHECK(!probe::NotNull(p, "p", stats));
  CHECK(probe::NotNull(&p, "p", stats));
  CHECK(stats->error_count() == 1);  // one probe.null event (ok=false)

  CHECK_THROWS([&] {
    int& r = probe::CheckNotNull<int>(p, "p", stats);
    (void)r;
  });

  int v = 5;
  CHECK(probe::CheckNotNull<int>(&v, "v") == 5);

  // invariant check
  CHECK(probe::Check(true, "invariant", stats));
  CHECK(!probe::Check(false, "broken", stats));
  CHECK(stats->error_count() == 3);  // + probe.fail
  CHECK_THROWS([&] { probe::Check(false, "broken2", stats, /*throw=*/true); });

  std::cout << "  done\n";
}

void TestAnomalyDetector() {
  std::cout << "Test anomaly detector...\n";

  auto stats = std::make_shared<StatsMonitor>();
  probe::AnomalyDetector detector(stats, /*k=*/4.0);

  bool any_anomaly = false;
  for (int i = 1; i <= 10; ++i) any_anomaly |= detector.Feed(double(i), "latency");
  CHECK(!any_anomaly);  // 1..10 -> no outliers at k=4

  CHECK(detector.count() == 10);
  CHECK(detector.mean() > 5.4 && detector.mean() < 5.6);

  CHECK(detector.Feed(100.0, "latency"));  // huge outlier -> anomaly
  CHECK(stats->error_count() == 1);        // one probe.anomaly event

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== Metric Observability (flame graph + probes) Tests ===\n\n";
  TestFlameGraph();
  TestSampler();
  TestNullProbe();
  TestAnomalyDetector();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
            << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
