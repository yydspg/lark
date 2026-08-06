// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/exec/compute_graph.h"

#include <algorithm>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <utility>

namespace lark::column::exec {

using Clock = std::chrono::steady_clock;
using std::chrono::nanoseconds;

// ─────────────────────────────────────────────────────────────────────────────
// Run coordination state for a single execution.
// ─────────────────────────────────────────────────────────────────────────────
struct ComputeGraph::ModuleAcc {
  Clock::time_point start{};
  Clock::time_point end{};
  bool started = false;
  std::size_t count = 0;
};

struct ComputeGraph::RunState {
  std::atomic<std::size_t> remaining{0};
  std::promise<void> done;
  std::mutex mu;
  std::map<std::string, ModuleAcc> modules;
  nanoseconds busy{0};
  Clock::time_point exec_start{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Construction / wiring
// ─────────────────────────────────────────────────────────────────────────────

ComputeNode& ComputeGraph::AddNode(std::string id, std::string module,
                                   std::unique_ptr<TensorOp> op) {
  auto node = std::make_unique<ComputeNode>(std::move(id), std::move(module),
                                            std::move(op));
  ComputeNode* raw = node.get();
  by_id_.emplace(raw->id(), raw);
  nodes_.push_back(std::move(node));
  finalized_ = false;
  return *raw;
}

ComputeNode* ComputeGraph::Find(const std::string& id) const {
  auto it = by_id_.find(id);
  return it == by_id_.end() ? nullptr : it->second;
}

void ComputeGraph::AddDependency(const std::string& id,
                                 const std::string& dep_id) {
  ComputeNode* node = Find(id);
  ComputeNode* dep = Find(dep_id);
  if (node == nullptr)
    throw std::out_of_range("ComputeGraph::AddDependency: unknown node '" + id +
                            "'");
  if (dep == nullptr)
    throw std::out_of_range("ComputeGraph::AddDependency: unknown dep '" +
                            dep_id + "'");
  const auto& deps = node->dependencies();
  if (std::find(deps.begin(), deps.end(), dep) == deps.end()) {
    node->add_dependency(dep);
  }
  finalized_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Topological sort (Kahn's algorithm) + cycle detection.
// ─────────────────────────────────────────────────────────────────────────────
void ComputeGraph::Finalize() {
  std::unordered_map<const ComputeNode*, std::size_t> in_degree;
  std::unordered_map<const ComputeNode*, std::vector<const ComputeNode*>> outs;
  for (const auto& node : nodes_) {
    for (const ComputeNode* dep : node->dependencies()) {
      outs[dep].push_back(node.get());
      in_degree[node.get()]++;
    }
  }

  std::queue<const ComputeNode*> ready;
  for (const auto& node : nodes_) {
    if (in_degree[node.get()] == 0) ready.push(node.get());
  }

  topo_.clear();
  topo_.reserve(nodes_.size());
  while (!ready.empty()) {
    const ComputeNode* n = ready.front();
    ready.pop();
    topo_.push_back(const_cast<ComputeNode*>(n));
    for (const ComputeNode* next : outs[n]) {
      if (--in_degree[next] == 0) ready.push(next);
    }
  }

  if (topo_.size() != nodes_.size()) {
    throw std::runtime_error("ComputeGraph: dependency cycle detected");
  }
  finalized_ = true;
}

void ComputeGraph::ResetRunState() {
  for (auto& node : nodes_) node->ResetRunState();
}

namespace {
// Pre-register every tensor name the graph may write so parallel op writes
// never insert/rehash the underlying store (data-race free).
void ReserveOutputs(const ComputeGraph& graph, TensorStore& store) {
  for (const auto& node : graph.nodes()) {
    for (const auto& out : node->op().outputs()) {
      if (!out.empty()) store.EnsureNames({out});
    }
  }
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Sequential execution
// ─────────────────────────────────────────────────────────────────────────────
void ComputeGraph::Execute(context::ExecutionContext& ctx) {
  if (!finalized_) Finalize();
  ResetRunState();
  if (nodes_.empty()) return;
  ReserveOutputs(*this, ctx.store());

  RunState state;
  state.exec_start = Clock::now();
  state.remaining.store(nodes_.size());

  for (ComputeNode* node : topo_) {
    const auto start = Clock::now();
    if (ctx.has_monitor())
      ctx.monitor()->OnNodeStart(node->id(), node->op().op_type());
    {
      std::lock_guard<std::mutex> lock(state.mu);
      auto& acc = state.modules[node->module()];
      acc.count++;
      if (!acc.started) {
        acc.started = true;
        acc.start = start;
      }
    }

    std::exception_ptr err;
    bool ok = true;
    try {
      OpContext opctx(ctx.store());
      node->op().compute(opctx);
    } catch (...) {
      err = std::current_exception();
      ok = false;
    }

    const auto end = Clock::now();
    const auto elapsed = end - start;
    node->SetElapsed(elapsed);
    {
      std::lock_guard<std::mutex> lock(state.mu);
      state.busy += elapsed;
      auto& acc = state.modules[node->module()];
      acc.end = std::max(acc.end, end);
    }
    if (ctx.has_monitor()) {
      if (ok)
        ctx.monitor()->OnNodeEnd(node->id(), node->op().op_type(), elapsed);
      else
        ctx.monitor()->OnNodeError(node->id(), node->op().op_type(), err);
    }
    node->SetStatus(ok ? NodeStatus::kSuccess : NodeStatus::kFailed);
    if (!ok) node->SetError(err);
    node->done_event().Set();
  }

  const auto wall = Clock::now() - state.exec_start;
  ReportRunSummary(ctx, state, wall, /*workers=*/1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Asynchronous coroutine execution
// ─────────────────────────────────────────────────────────────────────────────
coro::FireAndForget ComputeGraph::SpawnNode(ComputeNode* node,
                                            coro::ThreadPool& pool,
                                            context::ExecutionContext& ctx,
                                            RunState& state) {
  // Hop onto the compute pool.
  co_await pool.Schedule();

  // Wait for every dependency; suspending releases the worker so a small pool
  // never deadlocks on a wide graph.
  for (ComputeNode* dep : node->dependencies()) {
    co_await dep->done_event();
  }

  const auto start = Clock::now();
  if (ctx.has_monitor())
    ctx.monitor()->OnNodeStart(node->id(), node->op().op_type());
  {
    std::lock_guard<std::mutex> lock(state.mu);
    auto& acc = state.modules[node->module()];
    acc.count++;
    if (!acc.started) {
      acc.started = true;
      acc.start = start;
    }
  }

  std::exception_ptr err;
  bool ok = true;
  try {
    OpContext opctx(ctx.store());
    node->op().compute(opctx);
  } catch (...) {
    err = std::current_exception();
    ok = false;
  }

  const auto end = Clock::now();
  const auto elapsed = end - start;
  node->SetElapsed(elapsed);
  {
    std::lock_guard<std::mutex> lock(state.mu);
    state.busy += elapsed;
    auto& acc = state.modules[node->module()];
    acc.end = std::max(acc.end, end);
  }
  if (ctx.has_monitor()) {
    if (ok)
      ctx.monitor()->OnNodeEnd(node->id(), node->op().op_type(), elapsed);
    else
      ctx.monitor()->OnNodeError(node->id(), node->op().op_type(), err);
  }
  node->SetStatus(ok ? NodeStatus::kSuccess : NodeStatus::kFailed);
  if (!ok) node->SetError(err);
  node->done_event().Set();

  if (state.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    state.done.set_value();
  }
  co_return;
}

void ComputeGraph::ExecuteAsync(coro::ThreadPool& pool,
                                context::ExecutionContext& ctx) {
  if (!finalized_) Finalize();
  ResetRunState();
  if (nodes_.empty()) return;
  ReserveOutputs(*this, ctx.store());

  RunState state;
  state.remaining.store(nodes_.size());
  state.exec_start = Clock::now();
  std::future<void> fut = state.done.get_future();

  for (const auto& node : nodes_) {
    SpawnNode(node.get(), pool, ctx, state);
  }

  fut.wait();

  const auto wall = Clock::now() - state.exec_start;
  ReportRunSummary(ctx, state, wall, pool.size());
}

void ComputeGraph::ReportRunSummary(context::ExecutionContext& ctx,
                                    const RunState& state,
                                    nanoseconds wall,
                                    std::size_t workers) {
  if (!ctx.has_monitor()) return;
  for (const auto& [module, acc] : state.modules) {
    ctx.monitor()->OnModuleEnd(module, acc.end - acc.start, acc.count);
  }
  double util = 0.0;
  if (wall.count() > 0 && workers > 0) {
    util = static_cast<double>(state.busy.count()) /
           static_cast<double>(wall.count() * workers);
    util = std::min(util, 1.0);
  }
  ctx.monitor()->OnCpuPressure("compute", util, state.busy, wall, workers);
}

}  // namespace lark::column::exec
