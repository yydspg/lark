// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include "rpc/service.h"

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// RpcServer: the server-side OOP abstraction of a transport endpoint.
//
//   auto server = backend->CreateServer(endpoint);
//   server->RegisterService(std::make_shared<RpcServiceImpl>("billing"));
//   server->Start();  // ... server->Stop();
// ─────────────────────────────────────────────────────────────────────────────
class RpcServer {
 public:
  virtual ~RpcServer() = default;

  virtual void RegisterService(std::shared_ptr<RpcService> service) = 0;
  virtual void Start() = 0;
  virtual void Stop() = 0;
};

}  // namespace lark::rpc
