// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for coro::batch — batch-parallel processing over coroutines
// (BatchMap / BatchTransform / BatchForEach / BatchReduce).

#include <atomic>
#include <iostream>
#include <string>
#include <vector>

#include "coro/coro.h"

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

// ─────────────────────────────────────────────────────────────────────────────
// goodsList scenario: several compute coroutines compute attributes / do
// complex field transforms on a long list, split into batches.
// ─────────────────────────────────────────────────────────────────────────────
struct Goods {
  double price;
  double discount = 0.0;
  double final_price = 0.0;
  std::string tag;
};

void TestBatchMapGoods() {
  std::cout << "Test BatchMap (goodsList scenario)...\n";
  lark::coro::ThreadPool pool(8);

  std::vector<Goods> goods;
  for (int i = 0; i < 1000; ++i) goods.push_back(Goods{static_cast<double>(i)});

  // multiple compute coroutines cooperatively compute attributes / transforms
  lark::coro::batch::BatchMap(goods, /*batch_size=*/0, /*workers=*/8, pool,
                              [](Goods& g) {
                                g.discount = g.price * 0.9;
                                g.final_price = g.discount + 1.0;
                                g.tag = g.price > 100.0 ? "expensive" : "cheap";
                              })
      .Wait();

  CHECK(goods.size() == 1000);
  bool ok = true;
  for (size_t i = 0; i < goods.size(); ++i) {
    const Goods& g = goods[i];
    if (g.discount != g.price * 0.9 || g.final_price != g.discount + 1.0) {
      ok = false;
      break;
    }
  }
  CHECK(ok);
  CHECK(goods[0].tag == "cheap");
  CHECK(goods[500].tag == "expensive");
  CHECK(goods[999].final_price == 999.0 * 0.9 + 1.0);

  // empty input
  std::vector<Goods> empty;
  lark::coro::batch::BatchMap(empty, 0, 8, pool, [](Goods&) {}).Wait();
  CHECK(true);

  std::cout << "  done\n";
}

void TestBatchTransform() {
  std::cout << "Test BatchTransform...\n";
  lark::coro::ThreadPool pool(4);

  std::vector<int> data;
  for (int i = 0; i < 100; ++i) data.push_back(i);

  auto out = lark::coro::batch::BatchTransform<int, std::string>(
                 data, 0, 4, pool, [](int v) { return std::to_string(v * 10); })
                 .Get();
  CHECK(out.size() == 100);
  CHECK(out[0] == "0" && out[9] == "90" && out[99] == "990");

  // order preserved under concurrency
  bool ordered = true;
  for (int i = 0; i < 100; ++i) {
    if (out[i] != std::to_string(i * 10)) ordered = false;
  }
  CHECK(ordered);

  // empty -> empty
  std::vector<int> empty;
  auto e = lark::coro::batch::BatchTransform<int, int>(empty, 0, 2, pool,
                                                       [](int v) { return v; })
               .Get();
  CHECK(e.empty());

  std::cout << "  done\n";
}

void TestBatchForEach() {
  std::cout << "Test BatchForEach...\n";
  lark::coro::ThreadPool pool(4);

  std::vector<int> data(50, 3);
  std::atomic<long long> sum{0};
  lark::coro::batch::BatchForEach(data, 0, 4, pool,
                                  [&](int v) { sum.fetch_add(v); })
      .Wait();
  CHECK(sum.load() == 150);

  std::cout << "  done\n";
}

void TestBatchReduce() {
  std::cout << "Test BatchReduce...\n";
  lark::coro::ThreadPool pool(4);

  std::vector<int> data;
  for (int i = 1; i <= 100; ++i) data.push_back(i);

  auto total = lark::coro::batch::BatchReduce<int, long long>(
                   data, 0, 4, pool, 0LL,
                   [](long long acc, int v) { return acc + v; },
                   [](long long a, long long b) { return a + b; })
                   .Get();
  CHECK(total == 5050);

  // max reduce
  auto mx = lark::coro::batch::BatchReduce<int, int>(
                data, 0, 4, pool, 0,
                [](int acc, int v) { return v > acc ? v : acc; },
                [](int a, int b) { return a > b ? a : b; })
                .Get();
  CHECK(mx == 100);

  // empty
  std::vector<int> empty;
  auto e = lark::coro::batch::BatchReduce<int, long long>(
               empty, 0, 2, pool, 42LL,
               [](long long acc, int) { return acc + 1; },
               [](long long a, long long b) { return a + b; })
               .Get();
  CHECK(e == 42LL);

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== coro::batch (batch-parallel) Tests ===\n\n";
  TestBatchMapGoods();
  TestBatchTransform();
  TestBatchForEach();
  TestBatchReduce();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
            << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
