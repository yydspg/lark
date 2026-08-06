// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "dag/op_executor.h"

#include <queue>
#include <utility>

#include "toolkit/time.h"

namespace lark::dag {

// ─────────────────────────────────────────────────────────────────────────────
// OpGraph
// ─────────────────────────────────────────────────────────────────────────────
const std::vector<Op*> OpGraph::kEmptyDeps;

OpGraph& OpGraph::AddOp(std::shared_ptr<Op> op, std::string id) {
  if (!op) throw std::invalid_argument("OpGraph: null op");
  if (id.empty()) id = op->Name();
  if (!by_id_.emplace(id, op.get()).second) {
    throw std::invalid_argument("OpGraph: duplicate node id '" + id + "'");
  }
  ops_.push_back(std::move(op));
  compiled_ = false;
  return *this;
}

OpGraph& OpGraph::AddAspect(std::shared_ptr<OpAspect> aspect) {
  if (aspect) aspects_.push_back(std::move(aspect));
  return *this;
}

OpGraph& OpGraph::DependsOn(const std::string& dep,
                            const std::string& dependent) {
  auto dep_it = by_id_.find(dep);
  auto depnd_it = by_id_.find(dependent);
  if (dep_it == by_id_.end()) {
    throw std::invalid_argument("OpGraph: unknown dependency '" + dep + "'");
  }
  if (depnd_it == by_id_.end()) {
    throw std::invalid_argument("OpGraph: unknown dependent '" + dependent +
                                "'");
  }
  deps_[depnd_it->second].push_back(dep_it->second);
  compiled_ = false;
  return *this;
}

const std::vector<Op*>& OpGraph::Compile() {
  if (compiled_) return order_;

  // Kahn's algorithm for topological order + cycle detection.
  std::unordered_map<const Op*, std::size_t> in_degree;
  std::unordered_map<const Op*, std::vector<Op*>> outs;
  for (const auto& op : ops_) {
    for (Op* dep : deps_[op.get()]) {
      outs[dep].push_back(op.get());
      in_degree[op.get()]++;
    }
  }

  std::queue<Op*> ready;
  for (const auto& op : ops_) {
    if (in_degree[op.get()] == 0) ready.push(op.get());
  }

  order_.clear();
  order_.reserve(ops_.size());
  while (!ready.empty()) {
    Op* op = ready.front();
    ready.pop();
    order_.push_back(op);
    for (Op* next : outs[op]) {
      if (--in_degree[next] == 0) ready.push(next);
    }
  }
  if (order_.size() != ops_.size()) {
    throw std::runtime_error("OpGraph: dependency cycle detected");
  }
  compiled_ = true;
  return order_;
}

// ─────────────────────────────────────────────────────────────────────────────
// OpExecutor
// ─────────────────────────────────────────────────────────────────────────────
void OpExecutor::Emit(const std::string& action, const std::string& name,
                      bool ok) {
  if (!monitor_) return;
  metric::Event e{"dag", action, name};
  e.ok = ok;
  monitor_->Emit(e);
}

void OpExecutor::SetStatus(const Op* op, OpStatus status) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_[op] = status;
}

OpStatus OpExecutor::Status(const Op* op) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = status_.find(op);
  return it == status_.end() ? OpStatus::kPending : it->second;
}

bool OpExecutor::AllSucceeded() const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& [op, st] : status_) {
    (void)op;
    if (st != OpStatus::kSuccess && st != OpStatus::kSkipped) return false;
  }
  return true;
}

coro::Task<void> OpExecutor::RunOp(OpGraph& graph, Op* op, Context& data) {
  // Hop onto the dedicated dag thread pool (all ops run asynchronously there).
  co_await pool_.Schedule();

  SetStatus(op, OpStatus::kRunning);
  Emit("op.start", op->Name());

  // Aspect: condition-based auto-skip (切面).
  for (const auto& aspect : graph.aspects()) {
    if (aspect->ShouldSkip(*op, data)) {
      SetStatus(op, OpStatus::kSkipped);
      Emit("op.skipped", op->Name());
      co_return;
    }
  }

  for (const auto& aspect : graph.aspects()) aspect->OnBefore(*op, data);

  const int64_t start = toolkit::time::NowNanos();
  std::exception_ptr error;
  try {
    co_await op->Execute(data);
  } catch (...) {
    error = std::current_exception();
  }
  const int64_t elapsed = toolkit::time::NowNanos() - start;

  const OpStatus status = error ? OpStatus::kFailed : OpStatus::kSuccess;
  SetStatus(op, status);
  for (const auto& aspect : graph.aspects()) aspect->OnAfter(*op, data, status);

  if (monitor_) {
    metric::Event e{"dag", status == OpStatus::kFailed ? "op.error" : "op.end",
                    op->Name()};
    e.duration = std::chrono::nanoseconds(elapsed);
    e.ok = error == nullptr;
    monitor_->Emit(e);
  }

  // A failed op does not poison the graph: dependents still run and can
  // observe the failure via the executor statuses.
  co_return;
}

coro::Future<void> OpExecutor::ExecuteAsync(OpGraph& graph, Context& data) {
  const auto& order = graph.Compile();

  std::unordered_map<const Op*, coro::Future<void>> futures;
  futures.reserve(order.size());
  for (Op* op : order) {
    std::vector<coro::Future<void>> dep_futures;
    for (Op* dep : graph.DepsOf(op)) {
      dep_futures.push_back(futures.at(dep));
    }
    auto ready = coro::Future<void>::AllOf(dep_futures);
    futures[op] = ready.ThenAsync([this, &graph, op, &data]() -> coro::Task<void> {
      co_await RunOp(graph, op, data);
    });
  }

  std::vector<coro::Future<void>> all;
  all.reserve(order.size());
  for (Op* op : order) all.push_back(futures.at(op));
  return coro::Future<void>::AllOf(all);
}

void OpExecutor::Execute(OpGraph& graph, Context& data) {
  ExecuteAsync(graph, data).Wait();
}

}  // namespace lark::dag
