// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "rpc/channel.h"
#include "rpc/endpoint.h"
#include "rpc/server.h"

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// RpcBackend: abstracts a concrete RPC framework (in-process / gRPC / brpc).
//
// A backend knows how to build channels and servers for its own wire format.
// Business code never names a framework directly — it picks a backend by name
// through RpcFactory:
//
//   auto backend = RpcFactory::Instance().Create("inproc");
//   auto channel = backend->CreateChannel(RpcEndpoint{"inproc", "billing"});
// ─────────────────────────────────────────────────────────────────────────────
class RpcBackend {
 public:
  virtual ~RpcBackend() = default;

  virtual const char* name() const noexcept = 0;
  virtual std::unique_ptr<RpcChannel> CreateChannel(const RpcEndpoint& endpoint) = 0;
  virtual std::unique_ptr<RpcServer> CreateServer(const RpcEndpoint& endpoint) = 0;
};

using RpcBackendFactory = std::function<std::unique_ptr<RpcBackend>()>;

// ─────────────────────────────────────────────────────────────────────────────
// RpcFactory: registry of RPC backends keyed by name (strategy pattern).
//
// The "inproc" backend is always registered. "grpc" / "brpc" are registered
// when the build is configured with LARK_WITH_GRPC / LARK_WITH_BRPC.
// ─────────────────────────────────────────────────────────────────────────────
class RpcFactory {
 public:
  static RpcFactory& Instance();

  void Register(std::string name, RpcBackendFactory factory);
  std::unique_ptr<RpcBackend> Create(const std::string& name) const;
  bool Contains(const std::string& name) const;
  std::vector<std::string> Available() const;

 private:
  RpcFactory();
  mutable std::mutex mutex_;
  std::unordered_map<std::string, RpcBackendFactory> factories_;
};

// Convenience: build the built-in in-process backend directly.
std::unique_ptr<RpcBackend> CreateInProcBackend();

}  // namespace lark::rpc
