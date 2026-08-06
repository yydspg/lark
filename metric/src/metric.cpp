// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "metric/metric.h"

#include <sstream>
#include <stdexcept>

namespace lark::metric {

// ─────────────────────────────────────────────────────────────────────────────
// LoggingMonitor
// ─────────────────────────────────────────────────────────────────────────────
LoggingMonitor::LoggingMonitor(std::ostream& os) : os_(os) {}

void LoggingMonitor::Emit(const Event& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  os_ << "[monitor] " << event.source << "." << event.action << " "
      << event.subject << (event.ok ? "" : " FAILED");
  if (event.duration.count() > 0) {
    os_ << " " << std::chrono::duration_cast<std::chrono::microseconds>(
                       event.duration)
                       .count()
               << "us";
  }
  for (const auto& [k, v] : event.attrs) {
    os_ << " " << k << "=" << v;
  }
  os_ << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// StatsMonitor
// ─────────────────────────────────────────────────────────────────────────────
void StatsMonitor::Emit(const Event& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++events_;
  if (!event.ok) ++errors_;
  ++count_by_action_[event.action];
  duration_by_action_[event.action] += event.duration;
}

std::size_t StatsMonitor::event_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return events_;
}

std::size_t StatsMonitor::error_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return errors_;
}

nanoseconds StatsMonitor::duration(const std::string& action) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = duration_by_action_.find(action);
  return it == duration_by_action_.end() ? nanoseconds::zero() : it->second;
}

std::string StatsMonitor::summary() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream os;
  os << "--- monitor stats: events=" << events_ << " errors=" << errors_
     << " ---\n";
  for (const auto& [action, count] : count_by_action_) {
    os << "  " << action << ": count=" << count
       << " duration="
       << std::chrono::duration_cast<std::chrono::microseconds>(
              duration_by_action_.at(action))
              .count()
       << "us\n";
  }
  return os.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// CompositeMonitor
// ─────────────────────────────────────────────────────────────────────────────
void CompositeMonitor::Add(std::shared_ptr<Monitor> child) {
  if (!child) return;
  std::lock_guard<std::mutex> lock(mutex_);
  children_.push_back(std::move(child));
}

void CompositeMonitor::Emit(const Event& event) {
  // Snapshot the children so a re-entrant Add() cannot invalidate iteration.
  std::vector<std::shared_ptr<Monitor>> children;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    children = children_;
  }
  for (const auto& child : children) {
    child->Emit(event);
  }
}

std::size_t CompositeMonitor::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return children_.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// MonitorFactory
// ─────────────────────────────────────────────────────────────────────────────
MonitorFactory::MonitorFactory() {
  factories_.emplace("null", [] { return std::make_shared<NullMonitor>(); });
  factories_.emplace("logging", [] { return std::make_shared<LoggingMonitor>(); });
  factories_.emplace("stats", [] { return std::make_shared<StatsMonitor>(); });
  factories_.emplace("composite", [] { return std::make_shared<CompositeMonitor>(); });
}

MonitorFactory& MonitorFactory::Instance() {
  static MonitorFactory instance;
  return instance;
}

void MonitorFactory::Register(std::string name, MonitorFactoryFn factory) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!factories_.emplace(std::move(name), std::move(factory)).second) {
    throw std::invalid_argument("MonitorFactory: duplicate monitor name");
  }
}

std::shared_ptr<Monitor> MonitorFactory::Create(const std::string& name) const {
  MonitorFactoryFn factory;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(name);
    if (it == factories_.end()) {
      throw std::out_of_range("MonitorFactory: unknown monitor '" + name + "'");
    }
    factory = it->second;
  }
  return factory();
}

bool MonitorFactory::Contains(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.find(name) != factories_.end();
}

std::vector<std::string> MonitorFactory::Available() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> names;
  names.reserve(factories_.size());
  for (const auto& [name, _] : factories_) names.push_back(name);
  return names;
}

}  // namespace lark::metric
