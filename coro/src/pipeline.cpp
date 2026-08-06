// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "coro/pipeline.h"

#include <utility>

#include "toolkit/time.h"

namespace lark::coro {

Pipeline::Pipeline(ThreadPool& pool, std::shared_ptr<metric::Monitor> monitor)
    : pool_(pool), monitor_(std::move(monitor)) {}

Pipeline& Pipeline::Add(std::string name, StepFn step) {
  Step s;
  s.name = std::move(name);
  s.fn = std::move(step);
  steps_.push_back(std::move(s));
  return *this;
}

Pipeline& Pipeline::AddSync(std::string name, SyncStepFn step) {
  return Add(std::move(name),
             [step = std::move(step)](Context& ctx) -> Task<void> {
               step(ctx);
               co_return;
             });
}

Pipeline& Pipeline::AddParallel(
    std::vector<std::pair<std::string, StepFn>> steps) {
  Step s;
  s.name = "parallel(" + std::to_string(steps.size()) + ")";
  s.parallel = std::move(steps);
  s.is_parallel = true;
  steps_.push_back(std::move(s));
  return *this;
}

void Pipeline::Emit(const std::string& name, std::chrono::nanoseconds elapsed,
                    bool ok) {
  if (!monitor_) return;
  metric::Event e{"coro", "pipeline.step", name};
  e.duration = elapsed;
  e.ok = ok;
  monitor_->Emit(e);
}

Future<void> Pipeline::RunStep(const Step& step) {
  return Future<void>::Async(
      [this, &step]() -> Task<void> {
        const int64_t start = toolkit::time::NowNanos();
        std::exception_ptr err;
        try {
          if (step.is_parallel) {
            // Run all branch steps concurrently; join via AllOf. Steps must
            // write DISTINCT Context fields (no locks by convention).
            std::vector<Future<void>> futures;
            for (const auto& [name, fn] : step.parallel) {
              futures.push_back(Future<void>::Async(
                  [this, name, fn]() -> Task<void> {
                    const int64_t s = toolkit::time::NowNanos();
                    try {
                      co_await fn(ctx_);
                      Emit(name, std::chrono::nanoseconds(toolkit::time::NowNanos() - s),
                           true);
                    } catch (...) {
                      Emit(name, std::chrono::nanoseconds(toolkit::time::NowNanos() - s),
                           false);
                      throw;
                    }
                    co_return;
                  },
                  pool_));
            }
            co_await Future<void>::AllOf(futures);
          } else {
            co_await step.fn(ctx_);
          }
        } catch (...) {
          err = std::current_exception();
        }
        Emit(step.name, std::chrono::nanoseconds(toolkit::time::NowNanos() - start),
             err == nullptr);
        if (err) std::rethrow_exception(err);
        co_return;
      },
      pool_);
}

Future<void> Pipeline::Run() {
  Future<void> fut = Future<void>::Just();
  for (const auto& step : steps_) {
    fut = fut.ThenAsync(
        [this, &step]() -> Task<void> { co_await RunStep(step); });
  }
  return fut;
}

}  // namespace lark::coro
