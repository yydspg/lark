// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>
#include <string>

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// RpcEndpoint: a transport address ("scheme://address").
//
//   inproc://billing          in-process service (see inproc_backend.h)
//   grpc://127.0.0.1:8080     gRPC endpoint  (requires LARK_WITH_GRPC)
//   brpc://127.0.0.1:8080     brpc endpoint  (requires LARK_WITH_BRPC)
// ─────────────────────────────────────────────────────────────────────────────
struct RpcEndpoint {
  std::string scheme;   // backend name the factory resolves
  std::string address;  // host:port or logical service name

  RpcEndpoint() = default;
  RpcEndpoint(std::string scheme, std::string address)
      : scheme(std::move(scheme)), address(std::move(address)) {}

  static RpcEndpoint Parse(const std::string& uri) {
    const size_t sep = uri.find("://");
    if (sep == std::string::npos) {
      // Bare address: default to the in-process backend (useful in tests).
      return RpcEndpoint{"inproc", uri};
    }
    return RpcEndpoint{uri.substr(0, sep),
                       uri.substr(sep + 3, std::string::npos)};
  }

  std::string uri() const { return scheme + "://" + address; }
};

}  // namespace lark::rpc
