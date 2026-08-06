// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for the toolkit module (str / result / time / scope / hash).

#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "toolkit/toolkit.h"

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

using namespace lark::toolkit;

// ─────────────────────────────────────────────────────────────────────────────
// str
// ─────────────────────────────────────────────────────────────────────────────
void TestStr() {
  std::cout << "Test str...\n";

  auto parts = str::Split("a,b,,c", ",", /*skip_empty=*/true);
  CHECK(parts.size() == 3 && parts[0] == "a" && parts[2] == "c");
  auto all = str::Split("a,b,,c", ",");
  CHECK(all.size() == 4 && all[2].empty());

  auto any = str::SplitAny("a b\tc", " \t");
  CHECK(any.size() == 3 && any[1] == "b");

  CHECK(str::Join(parts, "-") == "a-b-c");
  CHECK(str::Join(std::vector<std::string_view>{"x", "y"}, "+") == "x+y");

  CHECK(str::Trim("  hi  ") == "hi");
  CHECK(str::Trim("|x|", "|") == "x");
  CHECK(str::IsBlank("  \t "));
  CHECK(!str::IsBlank(" x "));

  CHECK(str::StartsWith("hello", "he"));
  CHECK(!str::StartsWith("hello", "hi"));
  CHECK(str::EndsWith("hello", "llo"));
  CHECK(str::Contains("hello world", "wor"));
  CHECK(!str::Contains("hello", "z"));

  CHECK(str::ToLower("HeLLo") == "hello");
  CHECK(str::ToUpper("HeLLo") == "HELLO");
  CHECK(str::ReplaceAll("a-b-a", "a", "x") == "x-b-x");

  int64_t i = 0;
  CHECK(str::ToInt64("-42", i) && i == -42);
  CHECK(!str::ToInt64("12x", i));
  double d = 0;
  CHECK(str::ToDouble("3.5", d) && d == 3.5);
  CHECK(!str::ToDouble("", d));

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// result
// ─────────────────────────────────────────────────────────────────────────────
void TestResult() {
  std::cout << "Test result...\n";

  auto ok = Result<int>::Ok(42);
  CHECK(ok.ok() && static_cast<bool>(ok) && ok.value() == 42);
  CHECK(ok.value_or(-1) == 42);

  auto bad = Result<int>::Err("boom");
  CHECK(!bad.ok() && !static_cast<bool>(bad));
  CHECK(bad.error() == "boom");
  CHECK(bad.value_or(-1) == -1);
  CHECK_THROWS([&] { (void)bad.value(); });
  CHECK_THROWS([&] { (void)ok.error(); });

  // copy semantics
  Result<int> copy = ok;
  CHECK(copy.value() == 42);

  // void specialization
  auto v = Result<void>::Ok();
  CHECK(v.ok());
  auto ve = Result<void>::Err("nope");
  CHECK(!ve.ok());
  CHECK(ve.error() == "nope");

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// time
// ─────────────────────────────────────────────────────────────────────────────
void TestTime() {
  std::cout << "Test time...\n";

  using namespace std::chrono_literals;
  CHECK(time::FormatDuration(500ns) == "500ns");
  CHECK(time::FormatDuration(2500ns) == "2.5us");
  CHECK(time::FormatDuration(1500000ns) == "1.5ms");
  CHECK(time::FormatDuration(2100000000ns) == "2.1s");
  CHECK(time::FormatDurationNs(42) == "42ns");

  const int64_t t0 = time::NowNanos();
  const int64_t t1 = time::NowNanos();
  CHECK(t1 >= t0);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// scope
// ─────────────────────────────────────────────────────────────────────────────
void TestScope() {
  std::cout << "Test scope...\n";

  int ran = 0;
  {
    auto guard = MakeScopeGuard([&] { ran += 1; });
    CHECK(ran == 0);
  }
  CHECK(ran == 1);

  {
    auto guard = MakeScopeGuard([&] { ran += 10; });
    guard.Release();
  }
  CHECK(ran == 1);  // released -> no-op

  { LARK_DEFER(ran += 100;); }
  CHECK(ran == 101);

  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// hash
// ─────────────────────────────────────────────────────────────────────────────
void TestHash() {
  std::cout << "Test hash...\n";

  // FNV-1a 64 well-known values
  CHECK(hash::Fnv1a("") == 14695981039346656037ULL);
  CHECK(hash::Fnv1a("a") == 0xaf63dc4c8601ec8cULL);
  CHECK(hash::Fnv1a("hello") == hash::Fnv1a("hello"));
  CHECK(hash::Fnv1a("hello") != hash::Fnv1a("world"));
  CHECK(hash::Djb2("") == 5381);

  const uint64_t combined = hash::HashCombine(hash::Fnv1a("user"), 42);
  CHECK(combined != hash::Fnv1a("user"));

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== Toolkit Module Tests ===\n\n";
  TestStr();
  TestResult();
  TestTime();
  TestScope();
  TestHash();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
            << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
