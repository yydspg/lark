// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "rpc/grpc_backend.h"

#include <stdexcept>
#include <utility>

namespace lark::rpc {

// The gRPC adapter is only wired when LARK_WITH_GRPC is defined at build time
// (with gRPC linked). Without it we fail gracefully so call sites compile and
// behave uniformly across configurations.
#if defined(LARK_WITH_GRPC)

std::unique_ptr<RpcChannel> GrpcBackend::CreateChannel(
    const RpcEndpoint& endpoint) {
  // Real adapter: create a grpc::Channel from endpoint.address() and translate
  // RpcMessage <-> grpc::ByteBuffer at the boundary.
  (void)endpoint;
  throw std::runtime_error("GrpcBackend::CreateChannel: adapter not yet linked");
}

std::unique_ptr<RpcServer> GrpcBackend::CreateServer(
    const RpcEndpoint& endpoint) {
  (void)endpoint;
  throw std::runtime_error("GrpcBackend::CreateServer: adapter not yet linked");
}

#else

std::unique_ptr<RpcChannel> GrpcBackend::CreateChannel(
    const RpcEndpoint& endpoint) {
  (void)endpoint;
  throw std::runtime_error(
      "GrpcBackend is unavailable: build was configured without LARK_WITH_GRPC");
}

std::unique_ptr<RpcServer> GrpcBackend::CreateServer(
    const RpcEndpoint& endpoint) {
  (void)endpoint;
  throw std::runtime_error(
      "GrpcBackend is unavailable: build was configured without LARK_WITH_GRPC");
}

#endif

}  // namespace lark::rpc
