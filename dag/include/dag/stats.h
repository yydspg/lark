#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "dag/monitor.h"
#include "dag/node.h"

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
// StatsCollector: the built-in per-node monitoring/timing implementation.
//
// Business code can attach it to the Executor to get per-node elapsed times
// out of the box (no custom Monitor subclass needed):
//
//   lark::StatsCollector stats;
//   executor.SetMonitor(std::make_shared<lark::StatsCollector>(stats));
//   executor.Execute(graph, ctx);
//   std::cout << stats.stats().summary();
//
// Thread-safe: callbacks fire from multiple workers.
// ─────────────────────────────────────────────────────────────────────────────
class StatsCollector : public Monitor {
 public:
  StatsCollector() = default;

  void OnNodeStart(const Node&) override;
  void OnNodeSuccess(const Node& node, nanoseconds) override;
  void OnNodeFailure(const Node& node, exception_ptr, nanoseconds) override;
  void OnNodeFallback(const Node& node) override;
  void OnNodeSkipped(const Node& node) override;

  const ExecutionStats& stats() const noexcept { return stats_; }
  void Reset();

 private:
  void Record(const Node& node);

  mutable std::mutex mutex_;
  ExecutionStats stats_;
};

}  // namespace lark
