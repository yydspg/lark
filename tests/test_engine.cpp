// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for the feed / compute / fetch column engine (v4):
//   * multi-dtype tensors (int32/int64/float32/float64)
//   * Q8_0 block quantization (ggml-style)
//   * TensorStore (execution intermediates)
//   * ExecutionContext feed/compute/fetch phases
//   * execution layer: TensorOp / ComputeNode / ComputeGraph (sync + coroutine)
//   * backend factory (CPU)
//   * business layer: Module / DSL / Pipeline with anonymous temp nodes
//   * generic monitoring (module/feed/fetch timings, CPU pressure)

#include "column/column_engine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lark::column;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool cond, const char* expr, const char* file, int line) {
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::cerr << "FAIL: " << file << ":" << line << ": " << expr << "\n";
  }
}
#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)

void ExpectThrow(std::function<void()> fn, const char* what, const char* file,
                 int line) {
  ++g_checks;
  bool threw = false;
  try {
    fn();
  } catch (const std::exception&) {
    threw = true;
  }
  if (!threw) {
    ++g_failures;
    std::cerr << "FAIL: " << file << ":" << line << ": expected throw from "
              << what << "\n";
  }
}
#define CHECK_THROWS(fn) ExpectThrow((fn), #fn, __FILE__, __LINE__)

bool Near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// ─────────────────────────────────────────────────────────────────────────────
// Tensor: new dtypes
// ─────────────────────────────────────────────────────────────────────────────
void TestMultiDtypeTensors() {
  std::cout << "Test multi-dtype tensors...\n";

  auto i32 = Tensor::from_data(std::vector<int32_t>{1, 2, 3, 4});
  CHECK(i32.dtype() == DType::kInt32);
  CHECK(i32.get<int32_t>(2) == 3);

  auto f32 = Tensor::from_data(std::vector<float>{1.5f, 2.5f, 3.5f});
  CHECK(f32.dtype() == DType::kFloat32);
  CHECK(std::fabs(f32.get<float>(1) - 2.5f) < 1e-6f);

  auto full32 = Tensor::full(4, 7.0, DType::kInt32);
  CHECK(full32.get<int32_t>(3) == 7);
  auto fullf = Tensor::full(4, 2.5, DType::kFloat32);
  CHECK(std::fabs(fullf.get<float>(0) - 2.5f) < 1e-6f);

  // dtype helpers
  CHECK(is_int(DType::kInt32) && is_float(DType::kFloat32));
  CHECK(dtype_name(DType::kQ8_0) == std::string("q8_0"));
  CHECK(dtype_size(DType::kInt32) == 4 && dtype_size(DType::kFloat32) == 4);
  CHECK(dtype_bytes(33, DType::kQ8_0) == 2 * sizeof(BlockQ8_0));

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Quantization (ggml-style Q8_0)
// ─────────────────────────────────────────────────────────────────────────────
void TestQuantization() {
  std::cout << "Test Q8_0 quantization...\n";

  std::vector<double> values;
  for (int i = 0; i < 100; ++i) values.push_back(static_cast<double>(i % 50));
  Tensor a = Tensor::from_data(values);

  Tensor q = a.quantize_q8_0();
  CHECK(q.dtype() == DType::kQ8_0);
  CHECK(q.size() == a.size());
  CHECK(q.num_blocks() == 4);  // 100 values / 32 per block

  Tensor d = q.dequantize();
  CHECK(d.dtype() == DType::kFloat64);
  double max_err = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    max_err = std::max(max_err, std::fabs(a.get<double>(i) - d.get<double>(i)));
  }
  // per-block scale = max/127, so worst-case error is scale/2 <= max/254
  CHECK(max_err < 0.5);

  // dot product round trip: dot(q(x), q(y)) ≈ dot(x, y)
  std::vector<double> yvals;
  for (int i = 0; i < 100; ++i) yvals.push_back(static_cast<double>((i * 3) % 50));
  Tensor b = Tensor::from_data(yvals);
  Tensor qb = b.quantize_q8_0();
  double exact = 0.0;
  for (size_t i = 0; i < a.size(); ++i) exact += values[i] * yvals[i];
  double approx = q.dot_q8_0(qb);
  CHECK(std::fabs(approx - exact) / exact < 0.1);

  // dequantize of non-quantized throws
  CHECK_THROWS([&] { Tensor::ones(3, DType::kFloat64).dequantize(); });

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// TensorStore
// ─────────────────────────────────────────────────────────────────────────────
void TestTensorStore() {
  std::cout << "Test TensorStore...\n";

  TensorStore store;
  store.set("x", Tensor::from_data(std::vector<int64_t>{1, 2, 3}));
  store.set("y", Tensor::from_data(std::vector<double>{1.5, 2.5}));
  CHECK(store.size() == 2);
  CHECK(store.has("x") && !store.has("z"));
  CHECK(store.names()[0] == "x");

  // Unlike TensorTable, a store allows differing lengths (filter/reduce).
  CHECK(store.get("x").size() == 3 && store.get("y").size() == 2);

  store.set("x", Tensor::from_data(std::vector<int64_t>{9}));  // replace
  CHECK(store.get("x").get<int64_t>(0) == 9);
  CHECK(store.names().size() == 2);

  store.erase("y");
  CHECK(!store.has("y"));

  // merge from a table + materialize
  TensorStore s2;
  TensorTable table;
  table.add("a", Tensor::from_data(std::vector<int64_t>{1, 2}));
  table.add("b", Tensor::from_data(std::vector<double>{0.5, 1.5}));
  s2.add_table(table);
  TensorTable out = s2.to_table({"a", "b"});
  CHECK(out.num_rows() == 2);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// ExecutionContext phases
// ─────────────────────────────────────────────────────────────────────────────
void TestExecutionContextPhases() {
  std::cout << "Test ExecutionContext phases...\n";

  context::ExecutionContext ctx;
  CHECK(ctx.phase() == context::RunPhase::kFeed);
  CHECK(std::string(ctx.phase_name()) == "feed");

  ctx.begin_phase(context::RunPhase::kFeed);
  ctx.end_phase();
  ctx.begin_phase(context::RunPhase::kCompute);
  ctx.end_phase();
  ctx.begin_phase(context::RunPhase::kFetch);
  ctx.end_phase();

  CHECK(ctx.phase_elapsed(context::RunPhase::kFeed).count() >= 0);
  CHECK(ctx.phase_elapsed(context::RunPhase::kCompute).count() >= 0);
  CHECK(ctx.phase_elapsed(context::RunPhase::kFetch).count() >= 0);
  CHECK(ctx.phase() == context::RunPhase::kFetch);

  ctx.begin_phase(context::RunPhase::kFeed);  // auto-finalizes fetch
  CHECK(ctx.phase() == context::RunPhase::kFeed);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution layer ops (via factory + OpContext)
// ─────────────────────────────────────────────────────────────────────────────
void TestExecutionOps() {
  std::cout << "Test execution-layer ops...\n";

  TensorStore store;
  store.set("a", Tensor::from_data(std::vector<int64_t>{1, 2, 3, 4}));
  store.set("b", Tensor::from_data(std::vector<int64_t>{10, 20, 30, 40}));
  store.set("f", Tensor::from_data(std::vector<double>{1.5, 2.5, 3.5, 4.5}));

  auto run = [&](const exec::OpSpec& spec) -> const Tensor& {
    auto op = exec::create_op(spec);
    exec::OpContext octx(store);
    op->compute(octx);
    return store.get(spec.outputs.front());
  };

  // add (int64)
  const Tensor& s = run(exec::OpSpec{"add", {"a", "b"}, {"s"}});
  CHECK(s.dtype() == DType::kInt64 && s.get<int64_t>(0) == 11 &&
        s.get<int64_t>(3) == 44);

  // mul with float -> promotes to float64
  const Tensor& p = run(exec::OpSpec{"mul", {"a", "f"}, {"p"}});
  CHECK(p.dtype() == DType::kFloat64 && Near(p.get<double>(0), 1.5));

  // scalar op
  const Tensor& sc = run(exec::OpSpec{"mul_scalar", {"a"}, {"sc"},
                                       {{"scalar", "2.0"}}});
  CHECK(sc.get<int64_t>(3) == 8);

  // comparison -> int64 mask
  const Tensor& mask = run(exec::OpSpec{"gt_scalar", {"a"}, {"mask"},
                                         {{"scalar", "2"}}});
  CHECK(mask.dtype() == DType::kInt64);
  CHECK(mask.get<int64_t>(0) == 0 && mask.get<int64_t>(2) == 1);

  // select (mask ? a : 0)
  const Tensor& sel = run(exec::OpSpec{"select", {"mask", "a", "b"}, {"sel"}});
  CHECK(sel.get<int64_t>(0) == 10 && sel.get<int64_t>(3) == 4);

  // filter
  const Tensor& flt = run(exec::OpSpec{"filter", {"a", "mask"}, {"flt"}});
  CHECK(flt.size() == 2 && flt.get<int64_t>(0) == 3 && flt.get<int64_t>(1) == 4);

  // reductions
  const Tensor& total = run(exec::OpSpec{"sum", {"a"}, {"total"}});
  CHECK(total.size() == 1 && Near(total.get<double>(0), 10.0));
  const Tensor& cnt = run(exec::OpSpec{"count", {"a"}, {"cnt"}});
  CHECK(cnt.get<int64_t>(0) == 4);
  const Tensor& mx = run(exec::OpSpec{"max", {"b"}, {"mx"}});
  CHECK(mx.get<int64_t>(0) == 40);

  // cast
  const Tensor& casted = run(exec::OpSpec{"cast", {"a"}, {"casted"},
                                           {{"dtype", "float32"}}});
  CHECK(casted.dtype() == DType::kFloat32 && Near(casted.get<float>(0), 1.0f, 1e-6f));

  // quantize / dequantize / dot through the store
  const Tensor& qa = run(exec::OpSpec{"quantize", {"f"}, {"qa"}});
  CHECK(qa.dtype() == DType::kQ8_0);
  const Tensor& dqa = run(exec::OpSpec{"dequantize", {"qa"}, {"dqa"}});
  CHECK(dqa.dtype() == DType::kFloat64);
  const Tensor& dotv = run(exec::OpSpec{"dot", {"qa", "qa"}, {"dotv"}});
  double expect = 0.0;
  for (size_t i = 0; i < 4; ++i) expect += std::pow(1.5 + i, 2);
  CHECK(std::fabs(dotv.get<double>(0) - expect) / expect < 0.1);

  // unknown op throws
  CHECK_THROWS([&] { exec::create_op(exec::OpSpec{"nope", {}, {"x"}}); });

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Backend factory
// ─────────────────────────────────────────────────────────────────────────────
void TestBackendFactory() {
  std::cout << "Test backend factory...\n";

  auto& factory = backend::BackendFactory::Instance();
  CHECK(factory.Contains("cpu"));
  bool has_cpu = false;
  for (const auto& n : factory.Available())
    if (n == "cpu") has_cpu = true;
  CHECK(has_cpu);

  auto backend = factory.Create("cpu");
  CHECK(backend->name() == std::string("cpu"));
  CHECK(backend->SupportsDType(DType::kInt32));
  CHECK(backend->SupportsDType(DType::kQ8_0));

  auto op = backend->CreateOp(exec::OpSpec{"add", {"a", "b"}, {"c"}});
  CHECK(std::string(op->op_type()) == "add");

  CHECK_THROWS([&] { factory.Create("gpu"); });

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeGraph: sync + async + cycle detection
// ─────────────────────────────────────────────────────────────────────────────
void TestComputeGraph() {
  std::cout << "Test ComputeGraph...\n";

  exec::ComputeGraph graph;
  graph.AddNode("n1", "m1", exec::create_op(
      exec::OpSpec{"mul_scalar", {"x"}, {"x2"}, {{"scalar", "2"}}}));
  graph.AddNode("n2", "m1", exec::create_op(
      exec::OpSpec{"add", {"x2", "x"}, {"x3"}}));
  graph.AddNode("n3", "m2", exec::create_op(
      exec::OpSpec{"sum", {"x3"}, {"total"}}));
  graph.AddDependency("n2", "n1");
  graph.AddDependency("n3", "n2");
  graph.Finalize();
  CHECK(graph.size() == 3);

  // sync run
  {
    context::ExecutionContext ctx;
    TensorStore& store = ctx.store();
    store.set("x", Tensor::from_data(std::vector<int64_t>{1, 2, 3}));
    auto stats = std::make_shared<monitor::StatsCollector>();
    ctx.set_monitor(stats);
    graph.Execute(ctx);
    const Tensor& total = store.get("total");
    CHECK(total.get<double>(0) == 18.0);  // sum(2x + x) = sum(3x)
    CHECK(stats->stats().ops.size() == 3);
    CHECK(stats->stats().modules.size() == 2);
    CHECK(!stats->stats().pressure.empty());
  }

  // async coroutine run
  {
    context::ExecutionContext ctx;
    TensorStore& store = ctx.store();
    store.set("x", Tensor::from_data(std::vector<int64_t>{1, 2, 3}));
    lark::coro::ThreadPool pool(4);
    graph.ExecuteAsync(pool, ctx);
    CHECK(store.get("total").get<double>(0) == 18.0);
  }

  // cycle detection
  {
    exec::ComputeGraph g;
    g.AddNode("a", "m", exec::create_op(exec::OpSpec{"neg", {"b"}, {"a"}}));
    g.AddNode("b", "m", exec::create_op(exec::OpSpec{"neg", {"a"}, {"b"}}));
    g.AddDependency("b", "a");
    g.AddDependency("a", "b");
    CHECK_THROWS([&] { g.Finalize(); });
  }

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// DSL parsing
// ─────────────────────────────────────────────────────────────────────────────
void TestDsl() {
  std::cout << "Test DSL...\n";

  auto ops = biz::dsl::parse(
      "scaled = x * 2\n"
      "score = scaled + y\n"
      "mask = x > 3\n"
      "filtered = filter(score, mask)\n"
      "total = sum(score)\n"
      "best = max(select(mask, score, 0))\n");
  CHECK(ops.size() >= 6);
  bool has_mul_scalar = false, has_sum = false, has_filter = false;
  for (const auto& op : ops) {
    if (op.type == "mul_scalar") has_mul_scalar = true;
    if (op.type == "sum") has_sum = true;
    if (op.type == "filter") has_filter = true;
  }
  CHECK(has_mul_scalar && has_sum && has_filter);

  // literals fold into scalar ops
  auto ops2 = biz::dsl::parse("v = a + 2");
  bool folded = false;
  for (const auto& op : ops2)
    if (op.type == "add_scalar") folded = true;
  CHECK(folded);

  CHECK_THROWS([&] { biz::dsl::parse("x = "); });
  CHECK_THROWS([&] { biz::dsl::parse("x = bogus_fn(a)"); });

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline end-to-end: feed → compute → fetch
// ─────────────────────────────────────────────────────────────────────────────
void TestPipeline() {
  std::cout << "Test Pipeline (feed/compute/fetch)...\n";

  auto feature = std::make_shared<biz::Module>("feature");
  feature->input("x").input("y").output("scaled").output("score")
      .output("filtered").output("total").output("max_score")
      .from_dsl(
          "scaled = x * 2\n"
          "score = scaled + y\n"
          "mask = x > 3\n"
          "filtered = filter(score, mask)\n"
          "total = sum(score)\n"
          "max_score = max(score)\n");

  biz::Pipeline pipeline;
  pipeline.add_module(feature);
  pipeline.compile();

  // The framework adds anonymous boundary/temp nodes on top of the 6 DSL
  // statements (which produce ~12 nodes); verify the graph is non-trivial.
  CHECK(pipeline.node_count() >= 12);

  // feed (行转列)
  std::vector<Row> rows;
  for (int i = 1; i <= 5; ++i)
    rows.push_back({Cell(int64_t(i)), Cell(int64_t(i * 10))});
  pipeline.feed({"x", "y"}, rows);
  CHECK(pipeline.context().store().has("x"));
  CHECK(pipeline.context().store().get("x").size() == 5);

  // compute
  pipeline.compute();

  // fetch (列转行)
  auto score_rows = pipeline.fetch({"scaled", "score"});
  CHECK(score_rows.size() == 5);
  CHECK(std::get<int64_t>(score_rows[0][1]) == 12);   // 2 + 10
  CHECK(std::get<int64_t>(score_rows[4][0]) == 10);   // 5 * 2

  auto filtered = pipeline.fetch({"filtered"});
  CHECK(filtered.size() == 2);
  CHECK(std::get<int64_t>(filtered[0][0]) == 48);  // rows with x > 3

  auto totals = pipeline.fetch({"total", "max_score"});
  CHECK(std::get<double>(totals[0][0]) == 180.0);
  CHECK(std::get<int64_t>(totals[0][1]) == 60);  // max keeps the input dtype

  CHECK(pipeline.fetch_scalar("total") == 180.0);

  // monitoring stats are populated
  const auto& stats = pipeline.stats();
  CHECK(stats.feed_elapsed.count() >= 0);
  CHECK(stats.compute_elapsed.count() >= 0);
  CHECK(stats.fetch_elapsed.count() >= 0);
  CHECK(!stats.modules.empty());
  CHECK(!stats.pressure.empty());
  for (const auto& m : stats.modules) {
    if (m.module == "feature") CHECK(m.node_count >= 12);
  }
  for (const auto& p : stats.pressure) {
    CHECK(p.pool == "compute");
    CHECK(p.utilization >= 0.0 && p.utilization <= 1.0);
    CHECK(p.workers > 0);
  }

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cross-module wiring: a module need not know how it is joined into the graph
// ─────────────────────────────────────────────────────────────────────────────
void TestModuleWiring() {
  std::cout << "Test cross-module wiring (anonymous nodes)...\n";

  // Module A produces "scaled"; Module B consumes it without knowing A.
  auto a = std::make_shared<biz::Module>("a");
  a->input("x").output("scaled").from_dsl("scaled = x * 2");

  auto b = std::make_shared<biz::Module>("b");
  b->input("x").input("scaled").output("final").from_dsl("final = scaled + x");

  auto c = std::make_shared<biz::Module>("c");  // explicit ordering only
  c->input("x").output("sumx").depends_on("a").from_dsl("sumx = sum(x)");

  biz::Pipeline pipeline;
  pipeline.add_module(a).add_module(b).add_module(c);
  pipeline.compile();

  std::vector<Row> rows = {{Cell(int64_t(1))}, {Cell(int64_t(2))}, {Cell(int64_t(3))}};
  pipeline.feed({"x"}, rows);
  pipeline.compute();

  auto out = pipeline.fetch({"final"});
  CHECK(std::get<int64_t>(out[0][0]) == 3);  // 2x + x = 3x; x = 1
  CHECK(std::get<int64_t>(out[1][0]) == 6);
  CHECK(pipeline.fetch_scalar("sumx") == 6.0);  // sum(1..3) runs after a

  // duplicate column producers are rejected
  auto a2 = std::make_shared<biz::Module>("a2");
  a2->input("x").output("dup").from_dsl("dup = x + 1");
  auto b2 = std::make_shared<biz::Module>("b2");
  b2->input("x").output("dup").from_dsl("dup = x + 2");
  biz::Pipeline bad;
  bad.add_module(a2).add_module(b2);
  CHECK_THROWS([&] { bad.compile(); });

  // unknown depends_on target is rejected
  auto d = std::make_shared<biz::Module>("d");
  d->input("x").output("v").depends_on("nope").from_dsl("v = x");
  biz::Pipeline bad2;
  bad2.add_module(d);
  CHECK_THROWS([&] { bad2.compile(); });

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Quantization inside a pipeline module
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineQuantization() {
  std::cout << "Test quantization inside a pipeline...\n";

  auto qmod = std::make_shared<biz::Module>("quant");
  qmod->input("f").output("max_err").output("sim")
      .from_dsl(
          "q = quantize(f)\n"
          "dq = dequantize(q)\n"
          "err = dq - f\n"
          "aerr = abs(err)\n"
          "max_err = max(aerr)\n"
          "sim = dot(quantize(f), quantize(f))\n");

  biz::Pipeline pipeline;
  pipeline.add_module(qmod);
  pipeline.compile();

  std::vector<Row> rows;
  for (int i = 0; i < 40; ++i) rows.push_back({Cell(double(i))});
  pipeline.feed({"f"}, rows);
  pipeline.compute();

  double max_err = pipeline.fetch_scalar("max_err");
  double sim = pipeline.fetch_scalar("sim");
  CHECK(max_err < 0.5);
  double expect = 0.0;
  for (int i = 0; i < 40; ++i) expect += double(i) * double(i);
  CHECK(std::fabs(sim - expect) / expect < 0.05);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Pure-code orchestration (no DSL)
// ─────────────────────────────────────────────────────────────────────────────
void TestCodeOrchestration() {
  std::cout << "Test pure-code module orchestration...\n";

  auto m = std::make_shared<biz::Module>("code");
  m->input("x").output("out");
  m->op(biz::ops::mul_scalar("scaled", "x", 3.0));
  m->op(biz::ops::add_scalar("out", "scaled", 1.0));

  biz::Pipeline pipeline;
  pipeline.add_module(m);
  pipeline.compile();

  std::vector<Row> rows = {{Cell(int64_t(1))}, {Cell(int64_t(2))}};
  pipeline.feed({"x"}, rows);
  pipeline.compute();
  auto out = pipeline.fetch({"out"});
  CHECK(std::get<int64_t>(out[0][0]) == 4);   // 1*3 + 1
  CHECK(std::get<int64_t>(out[1][0]) == 7);   // 2*3 + 1

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// run() convenience + repeated runs (graph reuse)
// ─────────────────────────────────────────────────────────────────────────────
void TestRunAndReuse() {
  std::cout << "Test run() convenience + graph reuse...\n";

  auto m = std::make_shared<biz::Module>("linear");
  m->input("x").output("y").from_dsl("y = x * 2 + 1");

  biz::Pipeline pipeline;
  pipeline.add_module(m);
  pipeline.compile();

  std::vector<Row> r1 = {{Cell(int64_t(1))}, {Cell(int64_t(2))}};
  auto out1 = pipeline.run({"x"}, r1, {"y"});
  CHECK(std::get<int64_t>(out1[0][0]) == 3);
  CHECK(std::get<int64_t>(out1[1][0]) == 5);

  // run again with fresh data — graph state is reset internally
  std::vector<Row> r2 = {{Cell(int64_t(10))}};
  auto out2 = pipeline.run({"x"}, r2, {"y"});
  CHECK(std::get<int64_t>(out2[0][0]) == 21);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression tests for column-computing edge cases
// ─────────────────────────────────────────────────────────────────────────────
void TestColumnEdgeCases() {
  std::cout << "Test column edge cases...\n";

  TensorStore store;

  // empty reductions must not read out of bounds
  auto run = [&](const exec::OpSpec& spec) -> const Tensor& {
    auto op = exec::create_op(spec);
    exec::OpContext octx(store);
    op->compute(octx);
    return store.get(spec.outputs.front());
  };
  store.set("empty", Tensor(DType::kInt64));
  const Tensor& mx = run(exec::OpSpec{"max", {"empty"}, {"mx"}});
  CHECK(mx.size() == 1 && mx.get<int64_t>(0) == 0);
  const Tensor& mn = run(exec::OpSpec{"min", {"empty"}, {"mn"}});
  CHECK(mn.get<int64_t>(0) == 0);
  const Tensor& sm = run(exec::OpSpec{"sum", {"empty"}, {"sm"}});
  CHECK(sm.get<double>(0) == 0.0);

  // length-mismatched binary ops must throw, not read out of bounds
  store.set("a3", Tensor::from_data(std::vector<int64_t>{1, 2, 3}));
  store.set("b2", Tensor::from_data(std::vector<int64_t>{1, 2}));
  CHECK_THROWS([&] { run(exec::OpSpec{"add", {"a3", "b2"}, {"bad"}}); });
  CHECK_THROWS([&] { run(exec::OpSpec{"gt", {"a3", "b2"}, {"bad2"}}); });

  // quantized zeros are zeroed (dequantize → all zero)
  Tensor zq = Tensor::zeros(40, DType::kQ8_0);
  Tensor zdq = zq.dequantize();
  bool all_zero = true;
  for (size_t i = 0; i < zdq.size(); ++i)
    if (zdq.get<double>(i) != 0.0) all_zero = false;
  CHECK(all_zero);

  // a column written twice inside one module is rejected at compile time
  auto dup = std::make_shared<biz::Module>("dup");
  dup->input("x").output("o");
  dup->op(biz::ops::add_scalar("o", "x", 1.0));
  dup->op(biz::ops::add_scalar("o", "x", 2.0));
  biz::Pipeline bad;
  bad.add_module(dup);
  CHECK_THROWS([&] { bad.compile(); });

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward references: a module registered FIRST may depend on a later module's
// output — the framework creates an anonymous placeholder node (占位), then
// replaces it (最终替换) once the producer module compiles.
// ─────────────────────────────────────────────────────────────────────────────
void TestPlaceholderForwardRefs() {
  std::cout << "Test placeholder forward references...\n";

  // A is registered FIRST and consumes B's output "b_out".
  auto a = std::make_shared<biz::Module>("a");
  a->input("x").input("b_out").output("final").from_dsl("final = b_out + x");
  auto b = std::make_shared<biz::Module>("b");
  b->input("x").output("b_out").from_dsl("b_out = x * 2");

  biz::Pipeline p;
  p.add_module(a).add_module(b);
  p.compile();

  // the placeholder was fully replaced — none remain in the final graph
  CHECK(!p.graph().HasPlaceholders());
  CHECK(p.graph().Find("@placeholder:b_out") == nullptr);

  // forward depends_on: A depends_on B (B registered after A)
  auto a2 = std::make_shared<biz::Module>("a2");
  a2->input("x").output("out").depends_on("b2").from_dsl("out = x + 1");
  auto b2 = std::make_shared<biz::Module>("b2");
  b2->input("x").output("marker").from_dsl("marker = x * 100");
  biz::Pipeline p2;
  p2.add_module(a2).add_module(b2);
  p2.compile();
  CHECK(!p2.graph().HasPlaceholders());
  const auto* dep_node = p2.graph().Find("a2/@dep:b2");
  CHECK(dep_node != nullptr);
  CHECK(!dep_node->dependencies().empty());  // wired to b2's tail (replaced)

  // execute the forward-ref graph
  p.feed({"x"}, {{Cell(int64_t(3))}});
  p.compute();
  CHECK(p.fetch_scalar("final") == 9.0);   // b_out = 6, final = 6 + 3

  p2.feed({"x"}, {{Cell(int64_t(2))}});
  p2.compute();
  CHECK(p2.fetch_scalar("out") == 3.0);
  CHECK(p2.fetch_scalar("marker") == 200.0);

  // a cycle across modules (via forward references) is still caught at Finalize
  auto ca = std::make_shared<biz::Module>("ca");
  ca->input("b_out").output("a_out").from_dsl("a_out = b_out + 1");
  auto cb = std::make_shared<biz::Module>("cb");
  cb->input("a_out").output("b_out").from_dsl("b_out = a_out + 1");
  biz::Pipeline cyc;
  cyc.add_module(ca).add_module(cb);
  CHECK_THROWS([&] { cyc.compile(); });

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== Column Engine v4 (feed / compute / fetch) Tests ===\n";
  std::cout << "SIMD backend: " << simd::backend_name() << "\n\n";

  try {
    TestMultiDtypeTensors();
    TestQuantization();
    TestTensorStore();
    TestExecutionContextPhases();
    TestExecutionOps();
    TestBackendFactory();
    TestComputeGraph();
    TestDsl();
    TestPipeline();
    TestModuleWiring();
    TestPipelineQuantization();
    TestCodeOrchestration();
    TestRunAndReuse();
    TestColumnEdgeCases();
    TestPlaceholderForwardRefs();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
              << " checks passed\n";
    if (g_failures != 0) {
      std::cerr << g_failures << " check(s) FAILED\n";
      return 1;
    }
    std::cout << "All tests passed.\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "\nException: " << e.what() << "\n";
    return 1;
  }
}
