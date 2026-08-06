// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Generic RPC framework usage example.
//
// Business code programs against OOP interfaces (RpcService / RpcChannel /
// RpcServer / RpcBackend); concrete frameworks (in-process / gRPC / brpc) plug
// in behind RpcFactory.

#include <iostream>
#include <memory>
#include <string>

#include "rpc/rpc.h"

using namespace lark::rpc;

// 1) Service implemented by subclassing the interface (business code).
class GreeterService : public RpcService {
 public:
  const std::string& name() const noexcept override { return name_; }
  std::vector<std::string> methods() const override { return {"greet"}; }

  RpcStatus Dispatch(const std::string& method, const RpcMessage& request,
                     RpcMessage& response) override {
    if (method != "greet") {
      return RpcStatus::NotFound("no method '" + method + "'");
    }
    response = RpcMessage::FromString("hello, " + request.ToString() + "!");
    return RpcStatus::Ok();
  }

 private:
  std::string name_ = "greeter";
};

int main() {
  // 2) Pick a backend by name through the factory (strategy pattern).
  auto backend = RpcFactory::Instance().Create("inproc");
  std::cout << "backend: " << backend->name() << "\n";

  // 3) Server side: create a server, register services, start.
  auto server = backend->CreateServer(RpcEndpoint{"inproc", "greeter"});
  server->RegisterService(std::make_shared<GreeterService>());

  // Also a handler-based service (RpcServiceImpl).
  auto meta = std::make_shared<RpcServiceImpl>("meta");
  meta->AddMethod("ping", [](const RpcMessage& req, RpcMessage& res) {
    res = RpcMessage::FromString("pong:" + req.ToString());
    return RpcStatus::Ok();
  });
  server->RegisterService(meta);
  server->Start();

  // 4) Client side: one channel per endpoint, then Call().
  auto channel = backend->CreateChannel(RpcEndpoint{"inproc", "greeter"});
  RpcMessage request = RpcMessage::FromString("lark");
  RpcMessage response;
  RpcCallOptions options(std::chrono::milliseconds(500));

  RpcStatus st = channel->Call("greeter", "greet", request, response, options);
  if (st.ok()) {
    std::cout << "greet -> " << response.ToString() << "\n";
  } else {
    std::cout << "greet failed: " << st.ToString() << "\n";
  }

  auto meta_channel = backend->CreateChannel(RpcEndpoint{"inproc", "meta"});
  RpcMessage mreq = RpcMessage::FromString("x");
  RpcMessage mresp;
  RpcStatus mst = meta_channel->Call("meta", "ping", mreq, mresp, options);
  std::cout << "meta  -> " << (mst.ok() ? mresp.ToString() : mst.ToString())
            << "\n";

  server->Stop();
  return 0;
}
