// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for the unified monitor module and the abstract cache module
// (local + remote implementations, pluggable monitoring).

#include <chrono>
#include <coroutine>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "cache/cache.h"
#include "coro/thread_pool.h"
#include "monitor/monitor.h"
#include "rpc/rpc.h"

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

using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────────────────────
// Monitor module
// ─────────────────────────────────────────────────────────────────────────────
void TestMonitorModule() {
  std::cout << "Test monitor module...\n";

  auto& factory = lark::monitor::MonitorFactory::Instance();
  for (const auto& name : {"null", "logging", "stats", "composite"}) {
    CHECK(factory.Contains(name));
  }
  CHECK_THROWS([&] { factory.Create("nope"); });

  // stats monitor aggregates
  auto stats = std::make_shared<lark::monitor::StatsMonitor>();
  lark::monitor::Event hit{"cache", "cache.hit", "k1"};
  hit.duration = 10ms;
  stats->Emit(hit);
  stats->Emit(hit);
  lark::monitor::Event miss{"cache", "cache.miss", "k2"};
  miss.ok = false;
  stats->Emit(miss);
  CHECK(stats->event_count() == 3);
  CHECK(stats->error_count() == 1);
  CHECK(stats->duration("cache.hit") == 20ms);
  CHECK(stats->summary().find("cache.hit") != std::string::npos);

  // composite fans out
  auto composite = std::make_shared<lark::monitor::CompositeMonitor>();
  auto stats2 = std::make_shared<lark::monitor::StatsMonitor>();
  composite->Add(stats2);
  composite->Add(stats);
  composite->Emit(hit);
  CHECK(stats->event_count() == 4);
  CHECK(stats2->event_count() == 1);

  // logging monitor writes a line
  std::ostringstream os;
  lark::monitor::LoggingMonitor log(os);
  log.Emit(hit);
  CHECK(os.str().find("cache.hit") != std::string::npos);

  std::cout << "  done\n";
}

void TestCoroPoolMonitoring() {
  std::cout << "Test coro pool monitoring...\n";
  lark::coro::ThreadPool pool(1);
  auto stats = std::make_shared<lark::monitor::StatsMonitor>();
  pool.SetMonitor(stats);
  pool.Enqueue(std::noop_coroutine());
  std::this_thread::sleep_for(20ms);
  pool.Enqueue(std::noop_coroutine());
  std::this_thread::sleep_for(20ms);
  CHECK(stats->event_count() >= 2);
  std::cout << "  done\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cache module
// ─────────────────────────────────────────────────────────────────────────────

class CountingMonitor : public lark::monitor::Monitor {
 public:
  void Emit(const lark::monitor::Event& e) override {
    if (e.action == "cache.hit") ++hits;
    else if (e.action == "cache.miss") ++misses;
    else if (e.action == "cache.put") ++puts;
    else if (e.action == "cache.evict") ++evicts;
  }
  int hits = 0, misses = 0, puts = 0, evicts = 0;
};

void TestLocalCache() {
  std::cout << "Test LocalCache...\n";

  auto counter = std::make_shared<CountingMonitor>();
  lark::cache::CacheOptions options;
  options.monitor = counter;
  lark::cache::LocalCache cache(options);

  cache.Put("a", "1");
  cache.Put("b", "2");
  CHECK(cache.size() == 2);
  CHECK(cache.Get("a").value_or("?") == "1");
  CHECK(cache.Get("c").has_value() == false);  // miss

  CHECK(cache.Remove("b"));
  CHECK(!cache.Remove("b"));  // already gone
  CHECK(cache.size() == 1);
  cache.Clear();
  CHECK(cache.size() == 0);

  // monitoring events
  CHECK(counter->puts == 2);
  CHECK(counter->hits == 1);
  CHECK(counter->misses >= 1);

  std::cout << "  done\n";
}

void TestLocalCacheTtlAndEviction() {
  std::cout << "Test LocalCache TTL + LRU eviction...\n";

  // TTL expiry
  lark::cache::CacheOptions opts;
  lark::cache::LocalCache ttl(opts);
  ttl.Put("k", "v", 50ms);
  CHECK(ttl.Get("k").has_value());
  std::this_thread::sleep_for(80ms);
  CHECK(!ttl.Get("k").has_value());  // expired -> miss

  // LRU eviction with capacity 2
  auto counter = std::make_shared<CountingMonitor>();
  lark::cache::CacheOptions capped;
  capped.capacity = 2;
  capped.monitor = counter;
  lark::cache::LocalCache lru(capped);
  lru.Put("a", "1");
  lru.Put("b", "2");
  lru.Put("c", "3");             // evicts "a"
  CHECK(!lru.Get("a").has_value());
  CHECK(lru.Get("b").value_or("?") == "2");  // refresh "b" -> lru [b, c]
  lru.Put("d", "4");             // evicts "c"
  CHECK(!lru.Get("c").has_value());
  CHECK(lru.Get("d").value_or("?") == "4");
  CHECK(counter->evicts == 2);

  std::cout << "  done\n";
}

void TestRemoteCache() {
  std::cout << "Test RemoteCache (via in-process RPC)...\n";

  // Server side: a LocalCache wrapped in the cache RPC service.
  auto backend = lark::rpc::RpcFactory::Instance().Create("inproc");
  auto store = std::make_shared<lark::cache::LocalCache>(lark::cache::CacheOptions{});
  auto server = backend->CreateServer(lark::rpc::RpcEndpoint{"inproc", "cache"});
  server->RegisterService(std::make_shared<lark::cache::CacheRpcService>(store));
  server->Start();

  auto channel = backend->CreateChannel(lark::rpc::RpcEndpoint{"inproc", "cache"});
  std::shared_ptr<lark::rpc::RpcChannel> shared_channel = std::move(channel);

  auto counter = std::make_shared<CountingMonitor>();
  lark::cache::CacheOptions opts;
  opts.monitor = counter;

  auto& factory = lark::cache::CacheFactory::Instance();
  auto remote = factory.Create("remote", opts, shared_channel, "cache");
  CHECK(std::string(remote->name()) == "remote");

  // round trip through RPC -> server LocalCache
  remote->Put("user:1", "score:99");
  CHECK(remote->Get("user:1").value_or("?") == "score:99");
  CHECK(remote->size() == 1);

  // miss + removal
  CHECK(!remote->Get("user:2").has_value());
  CHECK(remote->Remove("user:1"));
  CHECK(!remote->Remove("user:1"));
  remote->Clear();
  CHECK(remote->size() == 0);

  // remote cache also emits cache.* events (and rpc.call events if requested)
  CHECK(counter->puts >= 1);
  CHECK(counter->hits >= 1);
  CHECK(counter->misses >= 1);

  server->Stop();
  std::cout << "  done\n";
}

void TestCacheFactory() {
  std::cout << "Test CacheFactory...\n";

  auto& factory = lark::cache::CacheFactory::Instance();
  CHECK(factory.Contains("local"));
  CHECK(factory.Contains("remote"));
  int types = 0;
  for (const auto& t : factory.Types()) {
    if (t == "local" || t == "remote") ++types;
  }
  CHECK(types == 2);

  // local needs no channel
  auto local = factory.Create("local", lark::cache::CacheOptions{});
  CHECK(std::string(local->name()) == "local");

  CHECK_THROWS([&] {
    factory.Create("memcached", lark::cache::CacheOptions{});
  });

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== Monitor + Cache Module Tests ===\n\n";
  TestMonitorModule();
  TestCoroPoolMonitoring();
  TestLocalCache();
  TestLocalCacheTtlAndEviction();
  TestRemoteCache();
  TestCacheFactory();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
            << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
