// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "rpc/backend.h"

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// GrpcBackend: gRPC transport adapter.
//
// This backend is only functional when the build is configured with
// LARK_WITH_GRPC=ON (and links gRPC). Otherwise CreateChannel / CreateServer
// return a kUnavailable status / throw, so the interface stays uniform.
// ─────────────────────────────────────────────────────────────────────────────
class GrpcBackend : public RpcBackend {
 public:
  const char* name() const noexcept override { return "grpc"; }
  std::unique_ptr<RpcChannel> CreateChannel(const RpcEndpoint& endpoint) override;
  std::unique_ptr<RpcServer> CreateServer(const RpcEndpoint& endpoint) override;
};

}  // namespace lark::rpc
