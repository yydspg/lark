// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "rpc/brpc_backend.h"

#include <stdexcept>
#include <utility>

namespace lark::rpc {

#if defined(LARK_WITH_BRPC)

std::unique_ptr<RpcChannel> BrpcBackend::CreateChannel(
    const RpcEndpoint& endpoint) {
  // Real adapter: create a brpc::Channel from endpoint.address() and translate
  // RpcMessage <-> brpc::Controller / protobuf messages at the boundary.
  (void)endpoint;
  throw std::runtime_error("BrpcBackend::CreateChannel: adapter not yet linked");
}

std::unique_ptr<RpcServer> BrpcBackend::CreateServer(
    const RpcEndpoint& endpoint) {
  (void)endpoint;
  throw std::runtime_error("BrpcBackend::CreateServer: adapter not yet linked");
}

#else

std::unique_ptr<RpcChannel> BrpcBackend::CreateChannel(
    const RpcEndpoint& endpoint) {
  (void)endpoint;
  throw std::runtime_error(
      "BrpcBackend is unavailable: build was configured without LARK_WITH_BRPC");
}

std::unique_ptr<RpcServer> BrpcBackend::CreateServer(
    const RpcEndpoint& endpoint) {
  (void)endpoint;
  throw std::runtime_error(
      "BrpcBackend is unavailable: build was configured without LARK_WITH_BRPC");
}

#endif

}  // namespace lark::rpc
