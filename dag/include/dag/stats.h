#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "dag/node.h"
#include "metric/metric.h"

namespace lark {

using std::chrono::nanoseconds;

// Per-node execution record captured by StatsCollector.
struct NodeRunStats {
  std::string id;
  std::string type;
  NodeStatus status;
  nanoseconds elapsed;
  nanoseconds started_at;  // offset from the run start (for waterfall charts)
};

// Aggregate view of one graph execution (timing + status per node).
struct ExecutionStats {
  nanoseconds total_elapsed{0};
  std::vector<NodeRunStats> nodes;

  // Human-readable waterfall summary ordered by start time.
  std::string summary() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// StatsCollector: the built-in per-node monitoring/timing implementation of
// the unified monitor abstraction.
//
// Attach it to the Executor to get per-node elapsed times out of the box:
//
//   lark::StatsCollector stats;
//   executor.SetMonitor(std::make_shared<lark::StatsCollector>(stats));
//   executor.Execute(graph, ctx);
//   std::cout << stats.stats().summary();
//
// The executor emits lark::metric::Event records; this collector turns the
// dag "node.*" events back into the domain ExecutionStats model.
// Thread-safe: callbacks fire from multiple workers.
// ─────────────────────────────────────────────────────────────────────────────
class StatsCollector : public metric::Monitor {
 public:
  StatsCollector() = default;

  void Emit(const metric::Event& event) override;

  const ExecutionStats& stats() const noexcept { return stats_; }
  void Reset();

 private:
  void HandleNodeEvent(const metric::Event& event);

  mutable std::mutex mutex_;
  ExecutionStats stats_;
};

}  // namespace lark
