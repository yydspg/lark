// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "metric/profile.h"

#include <algorithm>
#include <functional>
#include <sstream>

#include "toolkit/str.h"
#include "toolkit/time.h"

namespace lark::metric::profile {

namespace {

// Thread-local stack of active scope names; used to build full call paths.
thread_local std::vector<std::string> g_path_stack;

}  // namespace

void PathStackPush(const std::string& name) { g_path_stack.push_back(name); }
void PathStackPop() {
  if (!g_path_stack.empty()) g_path_stack.pop_back();
}
std::string PathJoin() { return toolkit::str::Join(g_path_stack, "::"); }

// ─────────────────────────────────────────────────────────────────────────────
// FlameGraphProfiler
// ─────────────────────────────────────────────────────────────────────────────
void FlameGraphProfiler::AddSample(const std::string& path, int64_t elapsed_ns) {
  if (!enabled()) return;
  std::lock_guard<std::mutex> lock(mutex_);
  total_ns_[path] += elapsed_ns;
  count_[path] += 1;
}

void FlameGraphProfiler::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  total_ns_.clear();
  count_.clear();
}

namespace {
// Internal tree builder node.
struct Node {
  std::string name;
  int64_t total = 0;
  int64_t count = 0;
  std::vector<Node> children;
};
}  // namespace

Frame FlameGraphProfiler::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);

  Node root;  // root aggregate

  std::function<void(Node&, const std::vector<std::string>&, std::size_t,
                     int64_t, int64_t)>
      insert = [&](Node& node, const std::vector<std::string>& parts,
                   std::size_t i, int64_t total, int64_t count) {
    Node* child = nullptr;
    for (auto& c : node.children) {
      if (c.name == parts[i]) {
        child = &c;
        break;
      }
    }
    if (child == nullptr) {
      node.children.push_back(Node{parts[i], 0, 0, {}});
      child = &node.children.back();
    }
    if (i + 1 == parts.size()) {
      child->total += total;
      child->count += count;
      return;
    }
    insert(*child, parts, i + 1, total, count);
  };

  for (const auto& [path, total] : total_ns_) {
    std::vector<std::string> parts = toolkit::str::Split(path, "::");
    insert(root, parts, 0, total, count_.at(path));
  }

  std::function<Frame(Node&)> to_frame = [&](Node& n) {
    Frame f;
    f.name = n.name;
    f.total_ns = n.total;
    f.count = n.count;
    for (auto& c : n.children) f.children.push_back(to_frame(c));
    int64_t kids = 0;
    for (const auto& c : f.children) kids += c.total_ns;
    f.self_ns = f.total_ns - kids;
    std::sort(f.children.begin(), f.children.end(),
              [](const Frame& a, const Frame& b) {
                return a.total_ns > b.total_ns;
              });
    return f;
  };

  Frame result;
  result.name = "root";
  for (auto& c : root.children) result.children.push_back(to_frame(c));
  return result;
}

std::string FlameGraphProfiler::TextFlameGraph() const {
  const Frame root = Snapshot();

  int64_t grand_total = 0;
  for (const auto& c : root.children) grand_total += c.total_ns;

  std::ostringstream os;
  os << "=== flame graph (total="
     << toolkit::time::FormatDuration(std::chrono::nanoseconds(grand_total))
     << ") ===\n";

  std::function<void(const Frame&, int)> print = [&](const Frame& f, int depth) {
    const double pct =
        grand_total > 0 ? 100.0 * static_cast<double>(f.total_ns) /
                              static_cast<double>(grand_total)
                        : 0.0;
    os << std::string(depth * 2, ' ') << f.name << " self="
       << toolkit::time::FormatDuration(std::chrono::nanoseconds(f.self_ns))
       << " total="
       << toolkit::time::FormatDuration(std::chrono::nanoseconds(f.total_ns))
       << " count=" << f.count << " (" << pct << "%)\n";
    for (const auto& c : f.children) print(c, depth + 1);
  };
  for (const auto& c : root.children) print(c, 1);
  return os.str();
}

std::string FlameGraphProfiler::CollapsedStacks() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream os;
  for (const auto& [path, total] : total_ns_) {
    (void)total;
    std::vector<std::string> parts = toolkit::str::Split(path, "::");
    std::reverse(parts.begin(), parts.end());  // leaf -> root
    os << toolkit::str::Join(parts, ";") << " " << count_.at(path) << "\n";
  }
  return os.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Scope
// ─────────────────────────────────────────────────────────────────────────────
Scope::Scope(FlameGraphProfiler& profiler, std::string name)
    : profiler_(profiler), name_(std::move(name)) {
  if (!profiler_.enabled()) return;
  PathStackPush(name_);
  start_ns_ = toolkit::time::NowNanos();
  active_ = true;
}

Scope::~Scope() {
  if (!active_) return;
  const int64_t elapsed = toolkit::time::NowNanos() - start_ns_;
  const std::string path = PathJoin();
  profiler_.AddSample(path, elapsed);
  PathStackPop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler
// ─────────────────────────────────────────────────────────────────────────────
Sampler::Sampler(FlameGraphProfiler& profiler,
                 std::chrono::milliseconds interval,
                 std::shared_ptr<Monitor> monitor)
    : profiler_(profiler),
      interval_(interval),
      monitor_(std::move(monitor)) {}

Sampler::~Sampler() { Stop(); }

void Sampler::Start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread([this] { Loop(); });
}

void Sampler::Stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void Sampler::Loop() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (running_.load()) {
    if (cv_.wait_for(lock, interval_) == std::cv_status::timeout &&
        running_.load()) {
      const Frame root = profiler_.Snapshot();
      int64_t total = 0;
      int64_t samples = 0;
      for (const auto& c : root.children) {
        total += c.total_ns;
        samples += c.count;
      }
      if (monitor_) {
        Event e{"metric", "profile.sample", "flamegraph"};
        e.attr_ns("total_ns", total);
        e.attr("samples", std::to_string(samples));
        monitor_->Emit(e);
      }
    }
  }
}

}  // namespace lark::metric::profile
