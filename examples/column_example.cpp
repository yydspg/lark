// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Column Engine v3 (Two-Layer Architecture) — Usage Examples
//
// Layer 1 (node/)   — Business nodes: define WHAT to compute (rules)
// Layer 2 (compute/) — Compute kernels: HOW to compute (SIMD-optimized)
//
// Data flows as TensorTable between nodes.
// Interfaces: feed data in via TensorTable, fetch results from TensorTable.

#include "column/column_engine.h"

#include <iostream>

using namespace lark::column;

// ── Example 1: Compute Layer — Direct Tensor Arithmetic ────────────────────
void example_compute_layer() {
  std::cout << "=== Example 1: Compute Layer (Tensor Arithmetic) ===\n\n";

  // Create tensors (pure data containers)
  Tensor a{1.0, 2.0, 3.0, 4.0, 5.0};
  Tensor b{10.0, 20.0, 30.0, 40.0, 50.0};

  // Compute layer: SIMD-optimized free functions
  auto sum = compute::add(a, b);
  auto product = compute::mul(a, b);
  auto scaled = compute::mul_scalar(a, 3.0);

  std::cout << "a = [1, 2, 3, 4, 5]\n";
  std::cout << "b = [10, 20, 30, 40, 50]\n\n";

  std::cout << "compute::add(a, b)   = [";
  for (size_t i = 0; i < sum.size(); ++i) {
    std::cout << sum.get<double>(i) << (i + 1 < sum.size() ? ", " : "");
  }
  std::cout << "]\n";

  std::cout << "compute::mul(a, b)   = [";
  for (size_t i = 0; i < product.size(); ++i) {
    std::cout << product.get<double>(i) << (i + 1 < product.size() ? ", " : "");
  }
  std::cout << "]\n";

  std::cout << "compute::mul_scalar(a, 3) = [";
  for (size_t i = 0; i < scaled.size(); ++i) {
    std::cout << scaled.get<double>(i) << (i + 1 < scaled.size() ? ", " : "");
  }
  std::cout << "]\n\n";

  // Reductions
  std::cout << "compute::sum(a)  = " << compute::sum(a) << "\n";
  std::cout << "compute::mean(a) = " << compute::mean(a) << "\n";
  std::cout << "compute::count(a) = " << compute::count(a) << "\n\n";
}

// ── Example 2: Business Node Layer — Graph Pipeline ────────────────────────
void example_node_pipeline() {
  std::cout << "=== Example 2: Business Node Pipeline ===\n\n";

  // Feed data in
  TensorTable input;
  input.add("x", Tensor::from_data(
                     std::vector<int64_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));

  std::cout << "Input: x = [1..10]\n\n";

  // Build pipeline of business nodes — each defines a RULE
  Graph graph;

  // Rule 1: Filter — keep only x > 3
  graph.add_node(std::make_unique<FilterNode>(
      [](const TensorTable& t) {
        return compute::gt_scalar(t.get("x"), 3);
      }));

  // Rule 2: Transform — compute x_squared = x² (as double)
  graph.add_node(std::make_unique<TransformNode>(
      "x_squared",
      [](const TensorTable& t) {
        return compute::square(t.get("x").to_double());
      }));

  // Rule 3: Transform — compute combined = x² + x
  graph.add_node(std::make_unique<TransformNode>(
      "combined",
      [](const TensorTable& t) {
        return compute::add(t.get("x_squared"), t.get("x").to_double());
      }));

  // Execute pipeline, fetch results
  auto result = graph.execute(std::move(input));

  std::cout << "Pipeline: filter(x>3) → x_squared=x² → combined=x²+x\n";
  std::cout << "Result: " << result.num_rows() << " rows\n\n";

  std::cout << "  x  | x_squared | combined\n";
  std::cout << "-----|-----------|---------\n";
  for (size_t i = 0; i < result.num_rows(); ++i) {
    std::cout << "  " << result.get("x").get<int64_t>(i) << "  |    "
              << result.get("x_squared").get<double>(i) << "    |   "
              << result.get("combined").get<double>(i) << "\n";
  }

  // Aggregate results via compute layer
  std::cout << "\nAggregates (via compute layer):\n";
  std::cout << "  sum(x_squared)  = " << compute::sum(result.get("x_squared")) << "\n";
  std::cout << "  mean(combined)  = " << compute::mean(result.get("combined")) << "\n\n";
}

// ── Example 3: Feature Engineering with Two-Layer Design ───────────────────
void example_feature_engineering() {
  std::cout << "=== Example 3: Feature Engineering ===\n\n";

  // Simulate feature data
  const size_t N = 1000;
  auto feature_a = Tensor::full(N, 5.0, DType::kDouble);
  auto feature_b = Tensor::full(N, 3.0, DType::kDouble);

  // Fill with varied data
  for (size_t i = 0; i < N; ++i) {
    feature_a.set<double>(i, static_cast<double>(i % 100));
    feature_b.set<double>(i, static_cast<double>((i * 7) % 50));
  }

  // Feed data in
  TensorTable data;
  data.add("feature_a", std::move(feature_a));
  data.add("feature_b", std::move(feature_b));

  // Build feature engineering pipeline — business nodes define rules
  Graph pipeline;

  // Rule 1: Normalize feature_a → (x - mean)
  pipeline.add_node(std::make_unique<TransformNode>(
      "a_normalized",
      [](const TensorTable& t) {
        const auto& a = t.get("feature_a");
        double mean_val = compute::mean(a);
        return compute::sub_scalar(a, mean_val);
      }));

  // Rule 2: Interaction feature → a * b
  pipeline.add_node(std::make_unique<TransformNode>(
      "interaction",
      [](const TensorTable& t) {
        return compute::mul(t.get("feature_a"), t.get("feature_b"));
      }));

  // Rule 3: Polynomial feature → a²
  pipeline.add_node(std::make_unique<TransformNode>(
      "a_squared",
      [](const TensorTable& t) {
        return compute::square(t.get("feature_a"));
      }));

  // Execute and fetch results
  auto result = pipeline.execute(std::move(data));

  std::cout << "Generated " << result.num_cols() << " features from "
            << result.num_rows() << " samples\n";
  std::cout << "Features: ";
  for (const auto& name : result.names()) {
    std::cout << name << " ";
  }
  std::cout << "\n\n";

  std::cout << "  feature_a mean:    " << compute::mean(result.get("feature_a")) << "\n";
  std::cout << "  a_normalized mean: " << compute::mean(result.get("a_normalized")) << "\n";
  std::cout << "  interaction sum:   " << compute::sum(result.get("interaction")) << "\n";
  std::cout << "  a_squared sum:     " << compute::sum(result.get("a_squared")) << "\n\n";
}

int main() {
  std::cout << "Column Engine v3 — Two-Layer Architecture Examples\n";
  std::cout << "===================================================\n\n";
  std::cout << "Layer 1 (node/):   Business nodes — define WHAT to compute\n";
  std::cout << "Layer 2 (compute/): Compute kernels — HOW to compute (SIMD)\n";
  std::cout << "SIMD backend: " << simd::backend_name() << "\n\n";

  try {
    example_compute_layer();
    example_node_pipeline();
    example_feature_engineering();

    std::cout << "✅ All examples completed!\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "❌ Example failed: " << e.what() << "\n";
    return 1;
  }
}
