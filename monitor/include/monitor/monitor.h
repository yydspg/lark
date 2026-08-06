// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Unified monitoring abstraction.
//
// Every LARK module (dag / column / rpc / coro / cache) reports through a
// single generic interface: implementations are pluggable, so business code
// picks whichever monitor fits (dev logging, aggregate stats, fan-out to a
// metrics exporter, or a no-op in production).
//
//   auto mon = monitor::MonitorFactory::Instance().Create("logging");
//   mon->Emit(monitor::Event{"cache", "cache.hit", "user:42"});

#include <chrono>
#include <cstddef>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace lark::monitor {

using Clock = std::chrono::steady_clock;
using std::chrono::nanoseconds;

// ─────────────────────────────────────────────────────────────────────────────
// Event: a generic, transport/domain-agnostic observation.
//
//   source   — originating module: "dag", "column", "rpc", "coro", "cache", ...
//   action   — verb: "node.success", "cache.hit", "feed.end", "rpc.call", ...
//   subject  — primary target: node id, cache key, method name, ...
//   attrs    — flexible key/value details (stringly-typed for portability)
//   duration — elapsed time for the action (0 when not timing something)
//   ok       — whether the action succeeded
// ─────────────────────────────────────────────────────────────────────────────
struct Event {
  std::string source;
  std::string action;
  std::string subject;
  std::unordered_map<std::string, std::string> attrs;
  nanoseconds duration{0};
  bool ok = true;
  Clock::time_point timestamp = Clock::now();

  Event() = default;
  Event(std::string source, std::string action, std::string subject)
      : source(std::move(source)),
        action(std::move(action)),
        subject(std::move(subject)) {}

  // Fluent attribute builder.
  Event& attr(std::string key, std::string value) {
    attrs[std::move(key)] = std::move(value);
    return *this;
  }
  Event& attr_ns(const std::string& key, long long ns) {
    attrs[key] = std::to_string(ns);
    return *this;
  }
  // Look up an attribute; returns nullptr when absent.
  const std::string* Get(const std::string& key) const {
    auto it = attrs.find(key);
    return it == attrs.end() ? nullptr : &it->second;
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Monitor: the abstract monitoring interface. Implementations are selected by
// business code (usually through MonitorFactory). All callbacks may fire from
// any worker thread, so implementations must be thread-safe.
// ─────────────────────────────────────────────────────────────────────────────
class Monitor {
 public:
  virtual ~Monitor() = default;
  virtual void Emit(const Event& event) = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Built-in implementations
// ─────────────────────────────────────────────────────────────────────────────

// Discards everything (production default).
class NullMonitor final : public Monitor {
 public:
  void Emit(const Event&) override {}
};

// Prints a human-readable line per event (dev / debugging).
class LoggingMonitor final : public Monitor {
 public:
  explicit LoggingMonitor(std::ostream& os = std::cout);
  void Emit(const Event& event) override;

 private:
  std::ostream& os_;
  std::mutex mutex_;
};

// Aggregate counters / durations per action + total error count.
class StatsMonitor final : public Monitor {
 public:
  void Emit(const Event& event) override;

  std::size_t event_count() const;
  std::size_t error_count() const;
  nanoseconds duration(const std::string& action) const;
  // Human-readable report.
  std::string summary() const;

 private:
  mutable std::mutex mutex_;
  std::size_t events_ = 0;
  std::size_t errors_ = 0;
  std::map<std::string, std::size_t> count_by_action_;
  std::map<std::string, nanoseconds> duration_by_action_;
};

// Fans out every event to a set of child monitors (e.g. internal stats +
// user exporter).
class CompositeMonitor final : public Monitor {
 public:
  void Add(std::shared_ptr<Monitor> child);
  void Emit(const Event& event) override;
  std::size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<Monitor>> children_;
};

// ─────────────────────────────────────────────────────────────────────────────
// MonitorFactory: register / select monitor implementations by name.
// Registered by default: "null", "logging", "stats", "composite".
// ─────────────────────────────────────────────────────────────────────────────
using MonitorFactoryFn = std::function<std::shared_ptr<Monitor>()>;

class MonitorFactory {
 public:
  static MonitorFactory& Instance();

  void Register(std::string name, MonitorFactoryFn factory);
  std::shared_ptr<Monitor> Create(const std::string& name) const;
  bool Contains(const std::string& name) const;
  std::vector<std::string> Available() const;

 private:
  MonitorFactory();
  mutable std::mutex mutex_;
  std::unordered_map<std::string, MonitorFactoryFn> factories_;
};

}  // namespace lark::monitor
