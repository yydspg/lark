// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Column Engine v4 — feed / compute / fetch usage example.
//
//   feed    : 行转列 — row-oriented business records → columnar tensors
//   compute : 图节点编排计算 — modules declare sub-graphs (code or DSL); the
//             framework wires them into a global graph with anonymous/temp
//             nodes and runs it on the coroutine compute pool.
//   fetch   : 列转行 — result columns → business rows.
//
// Also demonstrates: backend factory, Q8_0 quantization, and the generic
// monitoring interface (module timings, feed time, CPU pressure).

#include "column/column_engine.h"

#include <cmath>
#include <iostream>
#include <memory>

using namespace lark::column;
using lark::column::biz::Module;
using lark::column::biz::Pipeline;

// ─────────────────────────────────────────────────────────────────────────────
// 1) Module declared with the mini-DSL
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<Module> BuildFeatureModule() {
  auto m = std::make_shared<Module>("feature");
  m->input("amount").input("base_rate")
      .output("risk").output("gross").output("q_err").output("similarity")
      .from_dsl(
          // 业务规则: 只关注自己的 subGraph 怎么编排、数据怎么来、产出什么
          "scaled     = amount * 2\n"
          "gross      = scaled + base_rate\n"
          "risk       = gross > 100\n"            // int64 mask
          "quant      = quantize(gross)\n"        // ggml-style Q8_0
          "dq         = dequantize(quant)\n"
          "err        = abs(dq - gross)\n"
          "q_err      = max(err)\n"
          "similarity = dot(quant, quant)\n");
  return m;
}

// ─────────────────────────────────────────────────────────────────────────────
// 2) Module declared in pure code (business layer, ergonomic)
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<Module> BuildMarginModule() {
  auto m = std::make_shared<Module>("margin");
  m->input("gross").output("net");
  m->op(biz::ops::mul_scalar("tax", "gross", 0.2));
  m->op(biz::ops::sub("net", "gross", "tax"));
  return m;
}

int main() {
  std::cout << "Column Engine v4 — feed / compute / fetch\n";
  std::cout << "SIMD backend: " << simd::backend_name() << "\n\n";

  // Backend factory: only the CPU backend is implemented.
  auto backend = backend::BackendFactory::Instance().Create("cpu");

  Pipeline pipeline(std::move(backend), /*compute_workers=*/4);
  pipeline.add_module(BuildFeatureModule());
  pipeline.add_module(BuildMarginModule());  // consumes "gross" from feature
  pipeline.compile();

  std::cout << "Compiled " << pipeline.node_count() << " graph nodes from "
            << pipeline.module_count() << " modules (anonymous/temp nodes "
            << "added by the framework).\n\n";

  // ── feed (行转列) ──────────────────────────────────────────────────────────
  std::vector<Row> rows;
  rows.push_back({Cell(int64_t(60)), Cell(double(5.0))});    // amount, base_rate
  rows.push_back({Cell(int64_t(120)), Cell(double(10.0))});
  rows.push_back({Cell(int64_t(40)), Cell(double(2.5))});
  pipeline.feed({"amount", "base_rate"}, rows);

  // ── compute (图节点编排计算) ───────────────────────────────────────────────
  pipeline.compute();

  // ── fetch (列转行) ─────────────────────────────────────────────────────────
  std::cout << "risk flag (gross > 100) and net margin:\n";
  auto out = pipeline.fetch({"risk", "net"});
  for (const auto& row : out) {
    std::cout << "  risk=" << std::get<int64_t>(row[0])
              << "  net=" << std::get<double>(row[1]) << "\n";
  }

  std::cout << "\nquantization round-trip error: "
            << pipeline.fetch_scalar("q_err") << "\n";
  std::cout << "quantized similarity (dot):   "
            << pipeline.fetch_scalar("similarity") << "\n";

  // ── generic monitoring ─────────────────────────────────────────────────────
  const auto& stats = pipeline.stats();
  std::cout << "\n" << stats.summary();

  return 0;
}
