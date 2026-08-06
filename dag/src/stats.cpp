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

void StatsCollector::HandleNodeEvent(const monitor::Event& event) {
  // Terminal node events carry the status / elapsed / started_at attributes;
  // "node.start" is informational only and is skipped here.
  const std::string* status_str = event.Get("status");
  if (status_str == nullptr) return;

  const std::string status = *status_str;
  NodeStatus ns = NodeStatus::kSuccess;
  if (status == "running") return;
  if (status == "failed")
    ns = NodeStatus::kFailed;
  else if (status == "fallback")
    ns = NodeStatus::kFallback;
  else if (status == "skipped")
    ns = NodeStatus::kSkipped;

  NodeRunStats rec;
  rec.id = event.subject;
  const std::string* type = event.Get("type");
  rec.type = type ? *type : "";
  rec.status = ns;
  const std::string* elapsed = event.Get("elapsed_ns");
  rec.elapsed = elapsed ? nanoseconds(std::stoll(*elapsed)) : nanoseconds::zero();
  const std::string* started = event.Get("started_ns");
  rec.started_at =
      started ? nanoseconds(std::stoll(*started)) : nanoseconds::zero();

  std::lock_guard<std::mutex> lock(mutex_);
  stats_.nodes.push_back(std::move(rec));
}

void StatsCollector::Emit(const monitor::Event& event) {
  if (event.source != "dag") return;
  if (event.action.rfind("node.", 0) == 0) {
    HandleNodeEvent(event);
  }
}

void StatsCollector::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.nodes.clear();
  stats_.total_elapsed = nanoseconds::zero();
}

}  // namespace lark
