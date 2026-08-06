// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>

#include "rpc/call_options.h"
#include "rpc/endpoint.h"
#include "rpc/message.h"
#include "rpc/status.h"

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// RpcChannel: the client-side OOP abstraction of a transport connection.
//
// Implementations wrap concrete frameworks (in-process, gRPC, brpc) while
// business code only depends on this interface:
//
//   auto channel = backend->CreateChannel(endpoint);
//   RpcMessage req = RpcMessage::FromString("...");
//   RpcMessage res;
//   RpcStatus st = channel->Call("billing", "charge", req, res, opts);
// ─────────────────────────────────────────────────────────────────────────────
class RpcChannel {
 public:
  virtual ~RpcChannel() = default;

  virtual RpcStatus Call(const std::string& service, const std::string& method,
                         const RpcMessage& request, RpcMessage& response,
                         const RpcCallOptions& options) = 0;
};

}  // namespace lark::rpc
