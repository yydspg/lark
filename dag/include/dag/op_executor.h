// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Executes an OpGraph fully asynchronously on the dedicated dag thread pool,
// composing per-op coroutine::Future chains: every op awaits its dependencies,
// then runs (with aspect hooks) on the pool.

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "coro/future.h"
#include "coro/pipeline.h"
#include "coro/task.h"
#include "coro/thread_pool.h"
#include "dag/op_graph.h"
#include "metric/metric.h"

namespace lark::dag {

class OpExecutor {
 public:
  // `dag_pool` is the dedicated dag thread pool (e.g.
  // coro::Pools::Get(coro::PoolKind::kDag)). All ops run on it.
  explicit OpExecutor(coro::ThreadPool& dag_pool,
                      std::shared_ptr<metric::Monitor> monitor = {})
      : pool_(dag_pool), monitor_(std::move(monitor)) {}

  OpExecutor(const OpExecutor&) = delete;
  OpExecutor& operator=(const OpExecutor&) = delete;

  void SetMonitor(std::shared_ptr<metric::Monitor> monitor) {
    monitor_ = std::move(monitor);
  }

  // Execute the graph asynchronously and block until every op finished
  // (or was skipped). `graph` and `data` must outlive this call.
  void Execute(OpGraph& graph, Context& data);

  // Non-blocking: returns a Future that completes when the graph drains. The
  // caller must keep `graph` and `data` alive until it resolves.
  coro::Future<void> ExecuteAsync(OpGraph& graph, Context& data);

  // Per-op status of the last execution.
  OpStatus Status(const Op* op) const;
  bool AllSucceeded() const;

 private:
  coro::Task<void> RunOp(OpGraph& graph, Op* op, Context& data);
  void SetStatus(const Op* op, OpStatus status);
  void Emit(const std::string& action, const std::string& name,
            bool ok = true);

  coro::ThreadPool& pool_;
  std::shared_ptr<metric::Monitor> monitor_;
  mutable std::mutex mutex_;
  std::unordered_map<const Op*, OpStatus> status_;
};

}  // namespace lark::dag
