// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/column_engine.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace lark::column;

// ── Tensor basics (pure data container) ────────────────────────────────────
void test_tensor_basics() {
  std::cout << "Testing Tensor basics...\n";

  // From initializer list (int64)
  Tensor a{int64_t(1), int64_t(2), int64_t(3), int64_t(4)};
  assert(a.size() == 4);
  assert(a.dtype() == DType::kInt64);
  assert(a.get<int64_t>(0) == 1);
  assert(a.get<int64_t>(3) == 4);

  // From initializer list (double)
  Tensor b{1.0, 2.5, 3.7};
  assert(b.size() == 3);
  assert(b.dtype() == DType::kDouble);
  assert(std::abs(b.get<double>(1) - 2.5) < 1e-10);

  // Factory methods
  auto z = Tensor::zeros(5, DType::kDouble);
  assert(z.size() == 5);
  assert(z.get<double>(0) == 0.0);

  auto o = Tensor::ones(3, DType::kInt64);
  assert(o.get<int64_t>(2) == 1);

  auto f = Tensor::full(4, 7.0, DType::kDouble);
  assert(f.get<double>(3) == 7.0);

  // From vector
  auto v = Tensor::from_data(std::vector<int64_t>{10, 20, 30});
  assert(v.size() == 3);
  assert(v.get<int64_t>(1) == 20);

  // Clone
  auto c = a.clone();
  assert(c.size() == 4);
  assert(c.get<int64_t>(0) == 1);

  // Append
  Tensor d(DType::kDouble);
  d.append_double(1.1);
  d.append_double(2.2);
  assert(d.size() == 2);

  // to_double conversion
  Tensor i{int64_t(10), int64_t(20)};
  auto dbl = i.to_double();
  assert(dbl.dtype() == DType::kDouble);
  assert(std::abs(dbl.get<double>(0) - 10.0) < 1e-10);

  std::cout << "  ✓ Tensor basics passed\n";
}

// ── Compute layer: arithmetic ──────────────────────────────────────────────
void test_compute_arithmetic() {
  std::cout << "Testing compute layer arithmetic...\n";

  Tensor a{1.0, 2.0, 3.0, 4.0};
  Tensor b{10.0, 20.0, 30.0, 40.0};

  // Element-wise via compute:: free functions
  auto sum = compute::add(a, b);
  assert(std::abs(sum.get<double>(0) - 11.0) < 1e-10);
  assert(std::abs(sum.get<double>(3) - 44.0) < 1e-10);

  auto diff = compute::sub(b, a);
  assert(std::abs(diff.get<double>(0) - 9.0) < 1e-10);

  auto prod = compute::mul(a, b);
  assert(std::abs(prod.get<double>(2) - 90.0) < 1e-10);

  auto quot = compute::div(b, a);
  assert(std::abs(quot.get<double>(1) - 10.0) < 1e-10);

  // Operator overloads (delegate to compute layer)
  auto s2 = a + b;
  assert(std::abs(s2.get<double>(0) - 11.0) < 1e-10);

  auto d2 = a - b;
  assert(std::abs(d2.get<double>(0) - (-9.0)) < 1e-10);

  // Int64 arithmetic
  Tensor x{int64_t(1), int64_t(2), int64_t(3)};
  Tensor y{int64_t(10), int64_t(20), int64_t(30)};
  auto isum = x + y;
  assert(isum.get<int64_t>(1) == 22);

  std::cout << "  ✓ Compute arithmetic passed\n";
}

// ── Compute layer: scalar ops ──────────────────────────────────────────────
void test_compute_scalar() {
  std::cout << "Testing compute layer scalar ops...\n";

  Tensor a{1.0, 2.0, 3.0, 4.0};

  // Scalar via compute:: free functions
  auto r1 = compute::add_scalar(a, 10.0);
  assert(std::abs(r1.get<double>(0) - 11.0) < 1e-10);

  auto r2 = compute::mul_scalar(a, 3.0);
  assert(std::abs(r2.get<double>(2) - 9.0) < 1e-10);

  // Operator overloads with scalar
  auto r3 = a * 2.0;
  assert(std::abs(r3.get<double>(3) - 8.0) < 1e-10);

  auto r4 = 2.0 * a;
  assert(std::abs(r4.get<double>(3) - 8.0) < 1e-10);

  auto r5 = a + 100.0;
  assert(std::abs(r5.get<double>(0) - 101.0) < 1e-10);

  // Negation
  auto r6 = -a;
  assert(std::abs(r6.get<double>(0) - (-1.0)) < 1e-10);

  std::cout << "  ✓ Compute scalar ops passed\n";
}

// ── Compute layer: unary ops ───────────────────────────────────────────────
void test_compute_unary() {
  std::cout << "Testing compute layer unary ops...\n";

  Tensor a{1.0, 4.0, 9.0, 16.0};

  // Square
  auto sq = compute::square(a);
  assert(std::abs(sq.get<double>(0) - 1.0) < 1e-10);
  assert(std::abs(sq.get<double>(1) - 16.0) < 1e-10);

  // Sqrt
  auto sr = compute::sqrt(a);
  assert(std::abs(sr.get<double>(1) - 2.0) < 1e-10);
  assert(std::abs(sr.get<double>(3) - 4.0) < 1e-10);

  // Abs
  Tensor neg{-1.0, -2.0, 3.0};
  auto ab = compute::abs(neg);
  assert(std::abs(ab.get<double>(0) - 1.0) < 1e-10);
  assert(std::abs(ab.get<double>(1) - 2.0) < 1e-10);

  std::cout << "  ✓ Compute unary ops passed\n";
}

// ── Compute layer: reductions ──────────────────────────────────────────────
void test_compute_reductions() {
  std::cout << "Testing compute layer reductions...\n";

  Tensor a{1.0, 2.0, 3.0, 4.0, 5.0};

  assert(std::abs(compute::sum(a) - 15.0) < 1e-10);
  assert(std::abs(compute::mean(a) - 3.0) < 1e-10);
  assert(compute::count(a) == 5);

  // Int64 reductions
  Tensor b{int64_t(10), int64_t(20), int64_t(30)};
  assert(std::abs(compute::sum(b) - 60.0) < 1e-10);
  assert(std::abs(compute::mean(b) - 20.0) < 1e-10);

  std::cout << "  ✓ Compute reductions passed\n";
}

// ── Compute layer: comparisons ─────────────────────────────────────────────
void test_compute_comparisons() {
  std::cout << "Testing compute layer comparisons...\n";

  Tensor a{1.0, 5.0, 10.0, 15.0};

  // gt_scalar
  auto mask = compute::gt_scalar(a, 5.0);
  assert(mask.dtype() == DType::kInt64);
  assert(mask.get<int64_t>(0) == 0);  // 1 > 5 = false
  assert(mask.get<int64_t>(1) == 0);  // 5 > 5 = false
  assert(mask.get<int64_t>(2) == 1);  // 10 > 5 = true
  assert(mask.get<int64_t>(3) == 1);  // 15 > 5 = true

  // le_scalar
  auto mask2 = compute::le_scalar(a, 5.0);
  assert(mask2.get<int64_t>(0) == 1);  // 1 <= 5 = true
  assert(mask2.get<int64_t>(1) == 1);  // 5 <= 5 = true
  assert(mask2.get<int64_t>(2) == 0);  // 10 <= 5 = false

  // Tensor-tensor comparison
  Tensor b{2.0, 5.0, 8.0, 20.0};
  auto mask3 = compute::gt(a, b);
  assert(mask3.get<int64_t>(0) == 0);  // 1 > 2 = false
  assert(mask3.get<int64_t>(1) == 0);  // 5 > 5 = false
  assert(mask3.get<int64_t>(2) == 1);  // 10 > 8 = true
  assert(mask3.get<int64_t>(3) == 0);  // 15 > 20 = false

  std::cout << "  ✓ Compute comparisons passed\n";
}

// ── TensorTable ────────────────────────────────────────────────────────────
void test_tensor_table() {
  std::cout << "Testing TensorTable...\n";

  TensorTable table;
  table.add("x", Tensor{1.0, 2.0, 3.0});
  table.add("y", Tensor{10.0, 20.0, 30.0});

  assert(table.num_rows() == 3);
  assert(table.num_cols() == 2);
  assert(table.has("x"));
  assert(!table.has("z"));

  assert(std::abs(table.get("x").get<double>(1) - 2.0) < 1e-10);
  assert(std::abs(table.get("y").get<double>(2) - 30.0) < 1e-10);

  // Replace existing column
  table.add("x", Tensor{100.0, 200.0, 300.0});
  assert(std::abs(table.get("x").get<double>(0) - 100.0) < 1e-10);
  assert(table.num_cols() == 2);  // Still 2 columns

  // Clone
  auto cloned = table.clone();
  assert(cloned.num_rows() == 3);
  assert(std::abs(cloned.get("x").get<double>(0) - 100.0) < 1e-10);

  // Names
  auto names = table.names();
  assert(names.size() == 2);
  assert(names[0] == "x");
  assert(names[1] == "y");

  std::cout << "  ✓ TensorTable passed\n";
}

// ── Node layer: TransformNode ──────────────────────────────────────────────
void test_node_transform() {
  std::cout << "Testing TransformNode...\n";

  TensorTable input;
  input.add("x", Tensor{1.0, 2.0, 3.0, 4.0});

  Graph g;
  // Business node: defines rule "doubled = x * 2"
  g.add_node(std::make_unique<TransformNode>(
      "doubled",
      [](const TensorTable& t) {
        return compute::mul_scalar(t.get("x"), 2.0);
      }));
  // Business node: defines rule "squared = x²"
  g.add_node(std::make_unique<TransformNode>(
      "squared",
      [](const TensorTable& t) {
        return compute::square(t.get("x"));
      }));

  auto result = g.execute(std::move(input));

  assert(result.num_cols() == 3);
  assert(result.has("x"));
  assert(result.has("doubled"));
  assert(result.has("squared"));

  assert(std::abs(result.get("doubled").get<double>(0) - 2.0) < 1e-10);
  assert(std::abs(result.get("doubled").get<double>(3) - 8.0) < 1e-10);
  assert(std::abs(result.get("squared").get<double>(2) - 9.0) < 1e-10);

  std::cout << "  ✓ TransformNode passed\n";
}

// ── Node layer: FilterNode ─────────────────────────────────────────────────
void test_node_filter() {
  std::cout << "Testing FilterNode...\n";

  TensorTable input;
  input.add("value", Tensor{1.0, 5.0, 10.0, 15.0, 20.0});

  Graph g;
  // Business node: defines rule "keep rows where value > 5"
  g.add_node(std::make_unique<FilterNode>(
      [](const TensorTable& t) {
        return compute::gt_scalar(t.get("value"), 5.0);
      }));

  auto result = g.execute(std::move(input));

  assert(result.num_rows() == 3);  // 10, 15, 20
  assert(std::abs(result.get("value").get<double>(0) - 10.0) < 1e-10);
  assert(std::abs(result.get("value").get<double>(2) - 20.0) < 1e-10);

  std::cout << "  ✓ FilterNode passed\n";
}

// ── Node layer: AggregateNode ──────────────────────────────────────────────
void test_node_aggregate() {
  std::cout << "Testing AggregateNode...\n";

  TensorTable input;
  input.add("x", Tensor{1.0, 2.0, 3.0, 4.0, 5.0});

  Graph g;
  // Business node: defines rule "aggregate: sum and mean"
  g.add_node(std::make_unique<AggregateNode>(
      [](const TensorTable& t) {
        TensorTable out;
        auto s = compute::sum(t.get("x"));
        auto m = compute::mean(t.get("x"));
        out.add("sum_x", Tensor::from_data(std::vector<double>{s}));
        out.add("mean_x", Tensor::from_data(std::vector<double>{m}));
        return out;
      }));

  auto result = g.execute(std::move(input));

  assert(result.num_rows() == 1);
  assert(result.has("sum_x"));
  assert(result.has("mean_x"));
  assert(std::abs(result.get("sum_x").get<double>(0) - 15.0) < 1e-10);
  assert(std::abs(result.get("mean_x").get<double>(0) - 3.0) < 1e-10);

  std::cout << "  ✓ AggregateNode passed\n";
}

// ── Graph: pipeline (filter + transform + aggregate) ───────────────────────
void test_graph_pipeline() {
  std::cout << "Testing Graph pipeline...\n";

  TensorTable input;
  input.add("x", Tensor::from_data(std::vector<int64_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));

  Graph g;
  // Step 1: Filter — keep x > 5
  g.add_node(std::make_unique<FilterNode>(
      [](const TensorTable& t) {
        return compute::gt_scalar(t.get("x"), 5);
      }));
  // Step 2: Transform — doubled = x * 2 (convert to double first)
  g.add_node(std::make_unique<TransformNode>(
      "doubled",
      [](const TensorTable& t) {
        return compute::mul_scalar(t.get("x").to_double(), 2.0);
      }));

  auto result = g.execute(std::move(input));

  // After filter: x = [6,7,8,9,10], then doubled = [12,14,16,18,20]
  assert(result.num_rows() == 5);
  assert(result.has("x"));
  assert(result.has("doubled"));
  assert(std::abs(result.get("doubled").get<double>(0) - 12.0) < 1e-10);
  assert(std::abs(result.get("doubled").get<double>(4) - 20.0) < 1e-10);

  // Verify sum of doubled values: 12+14+16+18+20 = 80
  assert(std::abs(compute::sum(result.get("doubled")) - 80.0) < 1e-10);

  std::cout << "  ✓ Graph pipeline passed\n";
}

// ── SIMD vectorization verification ────────────────────────────────────────
void test_simd_large_tensor() {
  std::cout << "Testing SIMD with large tensor...\n";

  const size_t N = 10000;
  auto a = Tensor::full(N, 3.0, DType::kDouble);
  auto b = Tensor::full(N, 7.0, DType::kDouble);

  // These operations go through compute layer (auto-vectorized by compiler)
  auto sum = compute::add(a, b);
  auto prod = compute::mul(a, b);
  auto mixed = compute::add(compute::mul(a, b), compute::sub(a, b));

  assert(std::abs(sum.get<double>(0) - 10.0) < 1e-10);
  assert(std::abs(prod.get<double>(0) - 21.0) < 1e-10);
  assert(std::abs(mixed.get<double>(0) - 17.0) < 1e-10);  // 3*7 + 3-7 = 21-4 = 17

  // Verify reduction on large tensor
  assert(std::abs(compute::sum(a) - 30000.0) < 1e-10);
  assert(std::abs(compute::mean(a) - 3.0) < 1e-10);

  std::cout << "  ✓ SIMD large tensor passed\n";
}

int main() {
  std::cout << "=== Column Engine v3 (Two-Layer Architecture) Tests ===\n";
  std::cout << "SIMD backend: " << simd::backend_name() << "\n\n";

  try {
    // Layer 2: compute kernels
    test_tensor_basics();
    test_compute_arithmetic();
    test_compute_scalar();
    test_compute_unary();
    test_compute_reductions();
    test_compute_comparisons();

    // Data container
    test_tensor_table();

    // Layer 1: business nodes
    test_node_transform();
    test_node_filter();
    test_node_aggregate();

    // Pipeline
    test_graph_pipeline();

    // SIMD
    test_simd_large_tensor();

    std::cout << "\n✅ All tests passed!\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "\n❌ Test failed: " << e.what() << "\n";
    return 1;
  }
}
