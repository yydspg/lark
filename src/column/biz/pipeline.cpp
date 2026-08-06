// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/biz/pipeline.h"

#include <stdexcept>
#include <thread>

#include "column/biz/pipeline_compiler.h"

namespace lark::column::biz {

using namespace lark::column::context;

namespace {

using std::exception_ptr;

// Forwards every monitoring event to the internal StatsCollector AND to the
// optional user-supplied monitor, so both receive the full event stream.
class ChainingMonitor : public monitor::ExecutionMonitor {
 public:
  explicit ChainingMonitor(monitor::ExecutionMonitor* internal)
      : internal_(internal) {}

  void SetUser(monitor::ExecutionMonitor* user) { user_ = user; }

  void OnFeedStart() override {
    internal_->OnFeedStart();
    if (user_) user_->OnFeedStart();
  }
  void OnFeedEnd(nanoseconds elapsed) override {
    internal_->OnFeedEnd(elapsed);
    if (user_) user_->OnFeedEnd(elapsed);
  }
  void OnComputeStart() override {
    internal_->OnComputeStart();
    if (user_) user_->OnComputeStart();
  }
  void OnComputeEnd(nanoseconds elapsed) override {
    internal_->OnComputeEnd(elapsed);
    if (user_) user_->OnComputeEnd(elapsed);
  }
  void OnFetchStart() override {
    internal_->OnFetchStart();
    if (user_) user_->OnFetchStart();
  }
  void OnFetchEnd(nanoseconds elapsed) override {
    internal_->OnFetchEnd(elapsed);
    if (user_) user_->OnFetchEnd(elapsed);
  }
  void OnNodeStart(const std::string& id, const std::string& type) override {
    internal_->OnNodeStart(id, type);
    if (user_) user_->OnNodeStart(id, type);
  }
  void OnNodeEnd(const std::string& id, const std::string& type,
                 nanoseconds elapsed) override {
    internal_->OnNodeEnd(id, type, elapsed);
    if (user_) user_->OnNodeEnd(id, type, elapsed);
  }
  void OnNodeError(const std::string& id, const std::string& type,
                   const exception_ptr& error) override {
    internal_->OnNodeError(id, type, error);
    if (user_) user_->OnNodeError(id, type, error);
  }
  void OnModuleEnd(const std::string& module, nanoseconds elapsed,
                   std::size_t node_count) override {
    internal_->OnModuleEnd(module, elapsed, node_count);
    if (user_) user_->OnModuleEnd(module, elapsed, node_count);
  }
  void OnCpuPressure(const std::string& pool, double utilization,
                     nanoseconds busy, nanoseconds wall,
                     std::size_t workers) override {
    internal_->OnCpuPressure(pool, utilization, busy, wall, workers);
    if (user_) user_->OnCpuPressure(pool, utilization, busy, wall, workers);
  }

 private:
  monitor::ExecutionMonitor* internal_;
  monitor::ExecutionMonitor* user_ = nullptr;
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction / configuration
// ─────────────────────────────────────────────────────────────────────────────

Pipeline::Pipeline(std::unique_ptr<backend::Backend> backend, size_t compute_workers)
    : backend_(std::move(backend)),
      compute_workers_(compute_workers == 0 ? std::thread::hardware_concurrency()
                                            : compute_workers) {
  chaining_ = std::make_shared<ChainingMonitor>(&stats_);
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

Pipeline& Pipeline::set_monitor(std::shared_ptr<monitor::ExecutionMonitor> monitor) {
  monitor_ = std::move(monitor);
  static_cast<ChainingMonitor*>(chaining_.get())->SetUser(monitor_.get());
  return *this;
}

Pipeline& Pipeline::set_compute_workers(size_t n) {
  compute_workers_ = n == 0 ? std::thread::hardware_concurrency() : n;
  compute_pool_.reset();
  return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Monitoring helpers
// ─────────────────────────────────────────────────────────────────────────────

void Pipeline::NotifyFeedStart() { chaining_->OnFeedStart(); }
void Pipeline::NotifyFeedEnd() {
  chaining_->OnFeedEnd(ctx_.phase_elapsed(RunPhase::kFeed));
}
void Pipeline::NotifyComputeStart() { chaining_->OnComputeStart(); }
void Pipeline::NotifyComputeEnd() {
  chaining_->OnComputeEnd(ctx_.phase_elapsed(RunPhase::kCompute));
}
void Pipeline::NotifyFetchStart() { chaining_->OnFetchStart(); }
void Pipeline::NotifyFetchEnd() {
  chaining_->OnFetchEnd(ctx_.phase_elapsed(RunPhase::kFetch));
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
