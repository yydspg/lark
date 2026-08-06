// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/biz/pipeline.h"

#include <stdexcept>
#include <thread>

#include "column/biz/pipeline_compiler.h"

namespace lark::column::biz {

using namespace lark::column::context;

// ─────────────────────────────────────────────────────────────────────────────
// Construction / configuration
// ─────────────────────────────────────────────────────────────────────────────

Pipeline::Pipeline(std::unique_ptr<backend::Backend> backend, size_t compute_workers)
    : backend_(std::move(backend)),
      compute_workers_(compute_workers == 0 ? std::thread::hardware_concurrency()
                                            : compute_workers) {
  stats_ = std::make_shared<monitor::StatsCollector>();
  chaining_ = std::make_shared<::lark::metric::CompositeMonitor>();
  chaining_->Add(stats_);
  ctx_.set_monitor(chaining_);
}

Pipeline::~Pipeline() = default;

Pipeline& Pipeline::add_module(std::shared_ptr<Module> module) {
  if (module == nullptr)
    throw std::invalid_argument("Pipeline::add_module: null module");
  if (!module_names_.insert(module->name()).second)
    throw std::invalid_argument("Pipeline::add_module: duplicate module '" +
                                module->name() + "'");
  modules_.push_back(std::move(module));
  compiled_ = false;
  return *this;
}

Pipeline& Pipeline::set_backend(std::shared_ptr<backend::Backend> backend) {
  backend_ = std::move(backend);
  compiled_ = false;
  return *this;
}

Pipeline& Pipeline::set_monitor(std::shared_ptr<::lark::metric::Monitor> monitor) {
  monitor_ = std::move(monitor);
  // Rebuild the fan-out: internal StatsCollector + the user-supplied monitor.
  auto chaining = std::make_shared<::lark::metric::CompositeMonitor>();
  chaining->Add(stats_);
  if (monitor_) chaining->Add(monitor_);
  chaining_ = chaining;
  ctx_.set_monitor(chaining_);
  return *this;
}

Pipeline& Pipeline::set_compute_workers(size_t n) {
  compute_workers_ = n == 0 ? std::thread::hardware_concurrency() : n;
  compute_pool_.reset();
  return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Monitoring helpers (emit "column" phase events into the fan-out monitor)
// ─────────────────────────────────────────────────────────────────────────────

void Pipeline::NotifyFeedStart() {
  chaining_->Emit(::lark::metric::Event{"column", "feed.start", "feed"});
}
void Pipeline::NotifyFeedEnd() {
  ::lark::metric::Event e{"column", "feed.end", "feed"};
  e.duration = ctx_.phase_elapsed(RunPhase::kFeed);
  chaining_->Emit(e);
}
void Pipeline::NotifyComputeStart() {
  chaining_->Emit(::lark::metric::Event{"column", "compute.start", "compute"});
}
void Pipeline::NotifyComputeEnd() {
  ::lark::metric::Event e{"column", "compute.end", "compute"};
  e.duration = ctx_.phase_elapsed(RunPhase::kCompute);
  chaining_->Emit(e);
}
void Pipeline::NotifyFetchStart() {
  chaining_->Emit(::lark::metric::Event{"column", "fetch.start", "fetch"});
}
void Pipeline::NotifyFetchEnd() {
  ::lark::metric::Event e{"column", "fetch.end", "fetch"};
  e.duration = ctx_.phase_elapsed(RunPhase::kFetch);
  chaining_->Emit(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compilation
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string> Pipeline::output_columns() const {
  std::vector<std::string> out;
  for (const auto& module : modules_) {
    for (const auto& port : module->outputs()) out.push_back(port);
  }
  return out;
}

void Pipeline::compile() {
  graph_ = exec::ComputeGraph();
  compiled_ = true;

  PipelineCompiler compiler(graph_, *backend_);
  compiler.Compile(modules_, module_names_);
  graph_.Finalize();
}

// ─────────────────────────────────────────────────────────────────────────────
// Feed / compute / fetch
// ─────────────────────────────────────────────────────────────────────────────

void Pipeline::feed(const std::vector<std::string>& names,
                    const std::vector<Row>& rows) {
  ctx_.begin_phase(RunPhase::kFeed);
  NotifyFeedStart();
  TensorTable table = feed_table(names, rows);
  ctx_.set_feed(std::move(table));
  ctx_.store().clear();
  ctx_.store().add_table(ctx_.feed());
  ctx_.end_phase();
  NotifyFeedEnd();
}

void Pipeline::feed(TensorTable table) {
  ctx_.begin_phase(RunPhase::kFeed);
  NotifyFeedStart();
  ctx_.set_feed(std::move(table));
  ctx_.store().clear();
  ctx_.store().add_table(ctx_.feed());
  ctx_.end_phase();
  NotifyFeedEnd();
}

void Pipeline::compute() {
  if (!compiled_) compile();
  if (!compute_pool_) {
    compute_pool_ = std::make_unique<coro::ThreadPool>(compute_workers_);
  }
  ctx_.begin_phase(RunPhase::kCompute);
  NotifyComputeStart();
  graph_.ExecuteAsync(*compute_pool_, ctx_);
  ctx_.end_phase();
  NotifyComputeEnd();
}

std::vector<Row> Pipeline::fetch(const std::vector<std::string>& names) {
  ctx_.begin_phase(RunPhase::kFetch);
  NotifyFetchStart();
  TensorTable table = ctx_.store().to_table(names);
  ctx_.set_fetch(std::move(table));
  std::vector<Row> rows = fetch_rows(ctx_.fetch(), names);
  ctx_.end_phase();
  NotifyFetchEnd();
  return rows;
}

std::vector<Row> Pipeline::fetch() { return fetch(output_columns()); }

TensorTable Pipeline::fetch_table(const std::vector<std::string>& names) {
  ctx_.begin_phase(RunPhase::kFetch);
  NotifyFetchStart();
  TensorTable table = ctx_.store().to_table(names);
  ctx_.set_fetch(std::move(table));
  ctx_.end_phase();
  NotifyFetchEnd();
  return table;
}

double Pipeline::fetch_scalar(const std::string& name) {
  ctx_.begin_phase(RunPhase::kFetch);
  NotifyFetchStart();
  const Tensor& t = ctx_.store().get(name);
  const double v = ::lark::column::fetch_scalar(t);
  ctx_.end_phase();
  NotifyFetchEnd();
  return v;
}

std::vector<Row> Pipeline::run(const std::vector<std::string>& feed_names,
                               const std::vector<Row>& feed_rows,
                               const std::vector<std::string>& fetch_names) {
  feed(feed_names, feed_rows);
  compute();
  return fetch(fetch_names);
}

}  // namespace lark::column::biz
