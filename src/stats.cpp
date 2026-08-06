#include "dag/stats.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace lark {

namespace {
std::string FmtNs(nanoseconds ns) {
  const double us =
      std::chrono::duration_cast<std::chrono::microseconds>(ns).count() / 1000.0;
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
}  // namespace

std::string ExecutionStats::summary() const {
  std::ostringstream os;
  os << "--- dag run stats (nodes=" << nodes.size()
     << ", total=" << FmtNs(total_elapsed) << ") ---\n";
  std::vector<NodeRunStats> sorted = nodes;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const NodeRunStats& a, const NodeRunStats& b) {
                     return a.started_at < b.started_at;
                   });
  for (const auto& n : sorted) {
    os << "  [" << ToString(n.status) << "] " << n.id << " (start="
       << FmtNs(n.started_at) << ", elapsed=" << FmtNs(n.elapsed) << ")\n";
  }
  return os.str();
}

void StatsCollector::Record(const Node& node) {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.nodes.push_back(
      NodeRunStats{node.id(), node.type(), node.status(), node.elapsed(),
                   node.started_at()});
}

void StatsCollector::OnNodeStart(const Node&) {
  // Recorded on completion events below (start is captured via started_at()).
}

void StatsCollector::OnNodeSuccess(const Node& node, nanoseconds) {
  Record(node);
}

void StatsCollector::OnNodeFailure(const Node& node, exception_ptr, nanoseconds) {
  Record(node);
}

void StatsCollector::OnNodeFallback(const Node& node) { Record(node); }

void StatsCollector::OnNodeSkipped(const Node& node) { Record(node); }

void StatsCollector::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.nodes.clear();
  stats_.total_elapsed = nanoseconds::zero();
}

}  // namespace lark
