// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "column/backend/backend.h"
#include "column/biz/module.h"
#include "column/context/execution_context.h"
#include "column/exec/compute_graph.h"
#include "column/feed_fetch.h"
#include "column/monitor.h"
#include "dag/coro/thread_pool.h"

namespace lark::column::biz {

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline: the top-level business orchestrator — feed / compute / fetch.
//
//   Pipeline pipeline;                       // CPU backend, hardware-concurrency pool
//   pipeline.add_module(feature_module)
//           .add_module(score_module)
//           .set_monitor(make_shared<MyMonitor>());
//   pipeline.compile();
//
//   pipeline.feed({"age", "score"}, rows);   // 行转列 → execution store
//   pipeline.compute();                      // run the global graph (coroutines)
//   auto rows = pipeline.fetch({"risk"});    // 列转行
//
// Modules are joined into the global graph by the framework using anonymous
// (temp) boundary nodes and inferred dependency edges, so a module never
// needs to know how it is wired into the graph or whether the producers of
// its inputs are ready.
// ─────────────────────────────────────────────────────────────────────────────
class Pipeline {
 public:
  // `backend` selects the compute backend (default: CPU). `compute_workers`
  // sizes the compute pool (0 → hardware_concurrency).
  explicit Pipeline(std::unique_ptr<backend::Backend> backend =
                        backend::CreateCpuBackend(),
                    size_t compute_workers = 0);
  ~Pipeline();
  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;

  // ---- configuration ----------------------------------------------------
  Pipeline& add_module(std::shared_ptr<Module> module);
  Pipeline& set_backend(std::shared_ptr<backend::Backend> backend);
  Pipeline& set_monitor(std::shared_ptr<monitor::ExecutionMonitor> monitor);
  Pipeline& set_compute_workers(size_t n);

  // Compile registered modules into the global compute graph. Called
  // automatically before the first compute(). Throws std::invalid_argument /
  // std::runtime_error on wiring problems (duplicate module name, ambiguous
  // column producer, module dependency cycle, ...).
  void compile();

  // ---- feed (行转列) -----------------------------------------------------
  // Convert row-oriented records to columns and seed the execution store.
  // Call before compute().
  void feed(const std::vector<std::string>& names, const std::vector<Row>& rows);
  void feed(TensorTable table);

  // ---- compute -----------------------------------------------------------
  // Run the compiled graph asynchronously on the compute pool (coroutine
  // scheduling from the DAG framework). Blocks until every node finishes.
  void compute();

  // ---- fetch (列转行) -----------------------------------------------------
  std::vector<Row> fetch(const std::vector<std::string>& names);
  // Fetch every declared module output.
  std::vector<Row> fetch();
  TensorTable fetch_table(const std::vector<std::string>& names);
  double fetch_scalar(const std::string& name);

  // ---- convenience -------------------------------------------------------
  // feed + compute + fetch in one call; returns the fetched rows.
  std::vector<Row> run(const std::vector<std::string>& feed_names,
                       const std::vector<Row>& feed_rows,
                       const std::vector<std::string>& fetch_names);

  // ---- accessors ---------------------------------------------------------
  context::ExecutionContext& context() noexcept { return ctx_; }
  const context::ExecutionContext& context() const noexcept { return ctx_; }
  const exec::ComputeGraph& graph() const noexcept { return graph_; }
  size_t node_count() const noexcept { return graph_.size(); }
  size_t module_count() const noexcept { return modules_.size(); }

  // Aggregate stats (always populated by the internal StatsCollector).
  const monitor::RunStats& stats() const noexcept { return stats_.stats(); }

  // All declared output columns across registered modules.
  std::vector<std::string> output_columns() const;

 private:
  struct CompiledModule {
    std::string name;
    std::vector<std::string> output_names;
    std::string tail;  // last op node id
    std::vector<std::string> output_nodes;  // node ids producing declared outputs
  };

  // Compile-time wiring helpers.
  void CollectColumnProducers();
  std::vector<std::shared_ptr<Module>> TopoSortModules() const;
  void CompileModule(const std::shared_ptr<Module>& module);
  static std::string RemapTemp(const std::string& col,
                               const std::string& module_name,
                               std::unordered_map<std::string, std::string>& rename);
  std::string ResolveInputDep(
      const std::string& col,
      const std::unordered_map<std::string, std::string>& local,
      const std::string& module_name) const;

  // Monitoring helpers (always feed the internal collector; chain user monitor).
  void NotifyFeedStart();
  void NotifyFeedEnd();
  void NotifyComputeStart();
  void NotifyComputeEnd();
  void NotifyFetchStart();
  void NotifyFetchEnd();

  std::shared_ptr<backend::Backend> backend_;
  std::vector<std::shared_ptr<Module>> modules_;
  std::unordered_set<std::string> module_names_;
  std::shared_ptr<monitor::ExecutionMonitor> monitor_;
  monitor::StatsCollector stats_;
  std::shared_ptr<monitor::ExecutionMonitor> chaining_;  // internal + user
  size_t compute_workers_;

  context::ExecutionContext ctx_;
  exec::ComputeGraph graph_;
  std::unique_ptr<coro::ThreadPool> compute_pool_;
  bool compiled_ = false;

  // Compile-time state.
  std::unordered_map<std::string, std::string> column_producer_;  // col -> node id
  std::unordered_map<std::string, std::string> column_module_;    // col -> module
  std::unordered_map<std::string, CompiledModule> module_info_;   // module -> info
};

}  // namespace lark::column::biz
