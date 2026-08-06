// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "rpc/backend.h"

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// In-process RPC backend: routes calls directly to services registered in the
// same process, with no sockets or serialization dependencies. Useful for
// local dev, unit tests, and as a reference implementation for custom
// backends. The endpoint address is the service name ("inproc://billing").
// ─────────────────────────────────────────────────────────────────────────────
class InProcRegistry {
 public:
  static InProcRegistry& Instance();

  void Register(const std::string& name, RpcService* service);
  void Unregister(const std::string& name);
  RpcService* Find(const std::string& name) const;

 private:
  InProcRegistry() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, RpcService*> services_;
};

class InProcChannel : public RpcChannel {
 public:
  explicit InProcChannel(RpcEndpoint endpoint);

  RpcStatus Call(const std::string& service, const std::string& method,
                 const RpcMessage& request, RpcMessage& response,
                 const RpcCallOptions& options) override;

 private:
  // Finalize a call and (optionally) emit an "rpc.call" event.
  RpcStatus Finish(const RpcCallOptions& options, const std::string& service,
                   const std::string& method,
                   ::lark::metric::Clock::time_point start,
                   RpcStatus status);

  RpcEndpoint endpoint_;
};

class InProcServer : public RpcServer {
 public:
  explicit InProcServer(RpcEndpoint endpoint);
  ~InProcServer() override;

  void RegisterService(std::shared_ptr<RpcService> service) override;
  void Start() override;
  void Stop() override;

 private:
  RpcEndpoint endpoint_;
  std::vector<std::shared_ptr<RpcService>> services_;
  bool started_ = false;
};

class InProcBackend : public RpcBackend {
 public:
  const char* name() const noexcept override { return "inproc"; }
  std::unique_ptr<RpcChannel> CreateChannel(const RpcEndpoint& endpoint) override;
  std::unique_ptr<RpcServer> CreateServer(const RpcEndpoint& endpoint) override;
};

}  // namespace lark::rpc
