// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/biz/pipeline.h"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <thread>

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

void Pipeline::CollectColumnProducers() {
  column_module_.clear();
  for (const auto& module : modules_) {
    std::unordered_set<std::string> written;
    for (const auto& port : module->outputs()) written.insert(port);
    for (const auto& spec : module->subgraph().ops()) {
      for (const auto& out : spec.outputs) written.insert(out);
    }
    for (const auto& col : written) {
      if (col.empty() || col.front() == '@') continue;  // module-local temp
      auto [it, inserted] = column_module_.emplace(col, module->name());
      if (!inserted && it->second != module->name()) {
        throw std::invalid_argument(
            "Pipeline::compile: column '" + col + "' produced by both '" +
            it->second + "' and '" + module->name() + "'");
      }
    }
  }
}

std::vector<std::shared_ptr<Module>> Pipeline::TopoSortModules() const {
  // consumed columns per module
  std::unordered_map<std::string, std::unordered_set<std::string>> deps;
  for (const auto& module : modules_) {
    deps[module->name()];  // ensure entry
    for (const auto& dep : module->dependencies()) deps[module->name()].insert(dep);

    auto consumes = [&](const std::string& col) {
      auto it = column_module_.find(col);
      if (it == column_module_.end()) return;  // feed port
      const std::string& prod = it->second;
      if (prod != module->name()) deps[module->name()].insert(prod);
    };
    for (const auto& port : module->inputs()) consumes(port);
    for (const auto& spec : module->subgraph().ops()) {
      for (const auto& in : spec.inputs) {
        if (in.empty() || in.front() != '@') consumes(in);
      }
    }
  }

  // Validate explicit depends_on targets exist.
  for (const auto& module : modules_) {
    for (const auto& dep : module->dependencies()) {
      if (module_names_.count(dep) == 0)
        throw std::invalid_argument("Pipeline::compile: module '" +
                                    module->name() + "' depends_on unknown module '" +
                                    dep + "'");
    }
  }

  std::unordered_map<std::string, size_t> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> outs;
  for (const auto& module : modules_) {
    for (const auto& dep : deps[module->name()]) {
      outs[dep].push_back(module->name());
      in_degree[module->name()]++;
    }
  }

  std::queue<std::string> ready;
  std::vector<std::shared_ptr<Module>> order;
  std::unordered_set<std::string> visited;
  for (const auto& module : modules_) {
    if (in_degree[module->name()] == 0) ready.push(module->name());
  }
  while (!ready.empty()) {
    const std::string name = ready.front();
    ready.pop();
    for (const auto& module : modules_) {
      if (module->name() == name && visited.insert(name).second) {
        order.push_back(module);
        break;
      }
    }
    for (const auto& next : outs[name]) {
      if (--in_degree[next] == 0) ready.push(next);
    }
  }

  if (order.size() != modules_.size()) {
    throw std::runtime_error("Pipeline::compile: module dependency cycle detected");
  }
  return order;
}

// Rename a DSL temp column ("@t0") to a module-local name.
std::string Pipeline::RemapTemp(const std::string& col,
                                const std::string& module_name,
                                std::unordered_map<std::string, std::string>& rename) {
  auto it = rename.find(col);
  if (it != rename.end()) return it->second;
  const std::string mapped = "@" + module_name + "/" + col.substr(1);
  rename[col] = mapped;
  return mapped;
}

std::string Pipeline::ResolveInputDep(
    const std::string& col,
    const std::unordered_map<std::string, std::string>& local,
    const std::string& module_name) const {
  auto lit = local.find(col);
  if (lit != local.end()) return lit->second;
  auto pit = column_producer_.find(col);
  if (pit != column_producer_.end()) return pit->second;
  // Remaining columns are feed ports: data is seeded before compute() runs,
  // so no dependency edge is required.
  (void)module_name;
  return "";
}

void Pipeline::CompileModule(const std::shared_ptr<Module>& module) {
  CompiledModule cm;
  cm.name = module->name();

  std::unordered_map<std::string, std::string> local;  // col -> node id
  std::unordered_map<std::string, std::string> rename;  // DSL temp rename
  std::string first_node;

  auto add_node = [&](const std::string& node_id, exec::OpSpec spec) {
    graph_.AddNode(node_id, module->name(),
                   backend_->CreateOp(std::move(spec)));
    if (first_node.empty()) first_node = node_id;
  };
  auto remap = [&](std::string col) {
    if (!col.empty() && col.front() == '@')
      return RemapTemp(col, module->name(), rename);
    return col;
  };

  // ---- anonymous input boundary nodes (framework-managed wiring) --------
  for (const auto& port : module->inputs()) {
    const std::string node_id = module->name() + "/@in:" + port;
    add_node(node_id, exec::OpSpec{"identity", {port}, {port}});
    auto pit = column_producer_.find(port);
    if (pit != column_producer_.end() &&
        column_module_.at(port) != module->name()) {
      graph_.AddDependency(node_id, pit->second);
    }
    local[port] = node_id;
  }

  // ---- sub-graph ops ----------------------------------------------------
  size_t idx = 0;
  for (const auto& spec : module->subgraph().ops()) {
    exec::OpSpec ns = spec;
    for (auto& in : ns.inputs) in = remap(std::move(in));
    for (auto& out : ns.outputs) out = remap(std::move(out));

    const std::string node_id = module->name() + "/op" + std::to_string(idx++);

    // Materialize the execution node first (so dependency edges can resolve),
    // keeping `ns` intact for wiring.
    graph_.AddNode(node_id, module->name(), backend_->CreateOp(ns));

    for (const auto& in : ns.inputs) {
      const std::string dep = ResolveInputDep(in, local, module->name());
      if (!dep.empty()) graph_.AddDependency(node_id, dep);
    }
    for (const auto& out : ns.outputs) {
      local[out] = node_id;
      column_producer_[out] = node_id;
      column_module_[out] = module->name();
    }
    cm.tail = node_id;
  }

  // ---- declared outputs --------------------------------------------------
  for (const auto& port : module->outputs()) {
    auto it = local.find(port);
    if (it != local.end()) {
      cm.output_nodes.push_back(it->second);
    } else {
      // Pass-through output (an input port or a feed column): publish it via
      // an anonymous output node.
      const std::string node_id = module->name() + "/@out:" + port;
      add_node(node_id, exec::OpSpec{"identity", {port}, {port}});
      auto pit = column_producer_.find(port);
      if (pit != column_producer_.end() &&
          column_module_.at(port) != module->name()) {
        graph_.AddDependency(node_id, pit->second);
      }
      local[port] = node_id;
      column_producer_[port] = node_id;
      column_module_[port] = module->name();
      cm.output_nodes.push_back(node_id);
      cm.tail = node_id;
    }
    cm.output_names.push_back(port);
  }

  // ---- explicit module ordering edges (anonymous) -----------------------
  for (const auto& dep_name : module->dependencies()) {
    const std::string node_id = module->name() + "/@dep:" + dep_name;
    add_node(node_id, exec::OpSpec{"identity", {module->name()}, {module->name()}});
    const auto& dep_info = module_info_.at(dep_name);
    if (!dep_info.output_nodes.empty()) {
      for (const auto& out_node : dep_info.output_nodes)
        graph_.AddDependency(node_id, out_node);
    } else if (!dep_info.tail.empty()) {
      graph_.AddDependency(node_id, dep_info.tail);
    }
    if (!first_node.empty() && first_node != node_id)
      graph_.AddDependency(first_node, node_id);
    if (cm.tail.empty()) cm.tail = node_id;
  }

  module_info_[module->name()] = std::move(cm);
}

void Pipeline::compile() {
  graph_ = exec::ComputeGraph();
  module_info_.clear();
  column_producer_.clear();
  compiled_ = true;

  CollectColumnProducers();
  const auto order = TopoSortModules();
  for (const auto& module : order) {
    CompileModule(module);
  }
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
