// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <unordered_map>

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// RpcMessage: an opaque, transport-agnostic message.
//
// The payload is a byte string; headers carry metadata. Concrete backends may
// use their own wire format (protobuf / brpc / ...) and translate to/from
// RpcMessage at the adapter boundary.
// ─────────────────────────────────────────────────────────────────────────────
struct RpcMessage {
  std::string payload;
  std::unordered_map<std::string, std::string> headers;

  bool empty() const noexcept { return payload.empty() && headers.empty(); }
  void set_header(const std::string& key, std::string value) {
    headers[key] = std::move(value);
  }

  static RpcMessage FromString(std::string payload) {
    RpcMessage msg;
    msg.payload = std::move(payload);
    return msg;
  }
  std::string ToString() const { return payload; }
};

using RpcRequest = RpcMessage;
using RpcResponse = RpcMessage;

}  // namespace lark::rpc
