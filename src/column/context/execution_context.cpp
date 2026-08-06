// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/context/execution_context.h"

namespace lark::column::context {

using Clock = std::chrono::steady_clock;

void ExecutionContext::begin_phase(RunPhase p) {
  end_phase();
  phase_ = p;
  phase_start_ = Clock::now();
  phase_active_ = true;
}

void ExecutionContext::end_phase() {
  if (!phase_active_) return;
  phase_elapsed_[static_cast<std::size_t>(phase_)] += Clock::now() - phase_start_;
  phase_active_ = false;
}

void ExecutionContext::set_feed(TensorTable table) {
  feed_ = std::move(table);
}

void ExecutionContext::set_fetch(TensorTable table) {
  fetch_ = std::move(table);
}

void ExecutionContext::set_monitor(
    std::shared_ptr<monitor::ExecutionMonitor> monitor) {
  monitor_ = std::move(monitor);
}

}  // namespace lark::column::context
