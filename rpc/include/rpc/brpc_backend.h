// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "rpc/backend.h"

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// BrpcBackend: brpc (BaRPC) transport adapter.
//
// Only functional when built with LARK_WITH_BRPC=ON (and brpc's protobuf
// dependencies). Otherwise CreateChannel / CreateServer fail gracefully, so
// the interface stays uniform.
// ─────────────────────────────────────────────────────────────────────────────
class BrpcBackend : public RpcBackend {
 public:
  const char* name() const noexcept override { return "brpc"; }
  std::unique_ptr<RpcChannel> CreateChannel(const RpcEndpoint& endpoint) override;
  std::unique_ptr<RpcServer> CreateServer(const RpcEndpoint& endpoint) override;
};

}  // namespace lark::rpc
