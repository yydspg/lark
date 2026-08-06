// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Abstract cache + unified monitor usage example.
//
// Business code programs against the abstract Cache interface; the concrete
// implementation is chosen via CacheFactory ("local" / "remote"), and every
// operation reports into a pluggable lark::monitor::Monitor.

#include <iostream>
#include <memory>

#include "cache/cache.h"
#include "monitor/monitor.h"
#include "rpc/rpc.h"

using namespace lark;

int main() {
  // Pick a monitoring implementation per environment (dev / prod).
  auto monitor = monitor::MonitorFactory::Instance().Create("stats");

  // 1) Local cache.
  cache::CacheOptions local_opts;
  local_opts.monitor = monitor;
  auto local = cache::CacheFactory::Instance().Create("local", local_opts);

  local->Put("user:1", "{\"name\":\"alice\"}");
  if (auto v = local->Get("user:1")) {
    std::cout << "local hit: " << *v << "\n";
  }

  // 2) Remote cache served over the RPC framework.
  auto rpc_backend = rpc::RpcFactory::Instance().Create("inproc");
  auto server = rpc_backend->CreateServer(rpc::RpcEndpoint{"inproc", "cache"});
  server->RegisterService(std::make_shared<cache::CacheRpcService>(
      std::make_shared<cache::LocalCache>(cache::CacheOptions{})));
  server->Start();

  std::shared_ptr<rpc::RpcChannel> channel =
      rpc_backend->CreateChannel(rpc::RpcEndpoint{"inproc", "cache"});

  cache::CacheOptions remote_opts;
  remote_opts.monitor = monitor;
  auto remote = cache::CacheFactory::Instance().Create("remote", remote_opts,
                                                       channel, "cache");

  remote->Put("user:2", "{\"name\":\"bob\"}");
  if (auto v = remote->Get("user:2")) {
    std::cout << "remote hit: " << *v << "\n";
  }
  std::cout << "remote size: " << remote->size() << "\n";

  server->Stop();

  std::cout << "\n" << std::dynamic_pointer_cast<monitor::StatsMonitor>(monitor)->summary();
  return 0;
}
