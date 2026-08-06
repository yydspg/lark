// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "rpc/message.h"
#include "rpc/status.h"

namespace lark::rpc {

// Handler for a single RPC method.
using RpcMethodHandler = std::function<RpcStatus(const RpcMessage&, RpcMessage&)>;

// ─────────────────────────────────────────────────────────────────────────────
// RpcService: the OOP abstraction of a remotely callable service.
//
// Business code either subclasses RpcService directly or (more commonly) uses
// RpcServiceImpl to register method handlers. Backends dispatch into a service
// through Dispatch().
// ─────────────────────────────────────────────────────────────────────────────
class RpcService {
 public:
  virtual ~RpcService() = default;

  virtual const std::string& name() const noexcept = 0;
  virtual std::vector<std::string> methods() const = 0;

  // Invoke `method` with `request`; fill `response` on success.
  virtual RpcStatus Dispatch(const std::string& method,
                             const RpcMessage& request,
                             RpcMessage& response) = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// RpcServiceImpl: concrete service built from named method handlers.
//
//   auto svc = std::make_shared<RpcServiceImpl>("billing");
//   svc->AddMethod("charge", [](const RpcMessage& req, RpcMessage& res) {
//     res = RpcMessage::FromString("charged:" + req.ToString());
//     return RpcStatus::Ok();
//   });
// ─────────────────────────────────────────────────────────────────────────────
class RpcServiceImpl : public RpcService {
 public:
  explicit RpcServiceImpl(std::string name);

  RpcServiceImpl& AddMethod(std::string method, RpcMethodHandler handler);

  const std::string& name() const noexcept override { return name_; }
  std::vector<std::string> methods() const override;
  RpcStatus Dispatch(const std::string& method, const RpcMessage& request,
                     RpcMessage& response) override;

 private:
  std::string name_;
  std::unordered_map<std::string, RpcMethodHandler> methods_;
  mutable std::mutex mutex_;
};

}  // namespace lark::rpc
