// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Business-friendly async orchestration over a shared, lock-free Context.
//
// Steps are plain functions passed directly: each step reads fields from the
// Context, computes, and writes results back onto Context fields. Steps are
// composed into a Future chain (sequential Add / AddSync, or AddParallel for
// concurrent branches) and run on a coroutine pool.
//
// No locking is needed: sequential steps never overlap, and parallel branches
// must write DISTINCT Context fields (business convention — no contention).

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "coro/context.h"
#include "coro/future.h"
#include "coro/task.h"
#include "coro/thread_pool.h"
#include "metric/metric.h"

namespace lark::coro {

// An async step: reads/writes the shared Context.
using StepFn = std::function<Task<void>(Context&)>;
// A synchronous step.
using SyncStepFn = std::function<void(Context&)>;

class Pipeline {
 public:
  // `monitor` is optional; when set, each step emits a "coro" "pipeline.step"
  // event with its elapsed time.
  Pipeline(ThreadPool& pool, std::shared_ptr<metric::Monitor> monitor = {});
  ~Pipeline() = default;
  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;

  // Append a sequential async step (runs after the previous one completes).
  Pipeline& Add(std::string name, StepFn step);
  // Append a sequential synchronous step.
  Pipeline& AddSync(std::string name, SyncStepFn step);
  // Append a parallel branch: all steps run concurrently on the pool, then the
  // pipeline continues. Steps must write DISTINCT Context fields.
  Pipeline& AddParallel(std::vector<std::pair<std::string, StepFn>> steps);

  // Compose all steps into a single Future<void> and return it (the chain
  // starts immediately on the pool). The Pipeline must outlive the returned
  // future's execution.
  Future<void> Run();

  // The shared step context (lock-free; write distinct fields in parallel).
  Context& context() { return ctx_; }
  const Context& context() const { return ctx_; }

  std::size_t size() const { return steps_.size(); }

 private:
  struct Step {
    std::string name;
    StepFn fn;                                            // sequential step
    std::vector<std::pair<std::string, StepFn>> parallel;  // parallel branch
    bool is_parallel = false;
  };

  Future<void> RunStep(const Step& step);
  void Emit(const std::string& name, std::chrono::nanoseconds elapsed, bool ok);

  ThreadPool& pool_;
  std::shared_ptr<metric::Monitor> monitor_;
  std::vector<Step> steps_;
  Context ctx_;
};

}  // namespace lark::coro
