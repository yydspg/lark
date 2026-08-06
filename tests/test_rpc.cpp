// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

// Tests for the generic RPC framework (OOP interfaces + backend factory).

#include <iostream>
#include <memory>
#include <string>

#include "rpc/rpc.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool cond, const char* expr, const char* file, int line) {
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::cerr << "FAIL: " << file << ":" << line << ": " << expr << "\n";
  }
}
#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)

void ExpectThrow(std::function<void()> fn, const char* what, const char* file,
                 int line) {
  ++g_checks;
  bool threw = false;
  try {
    fn();
  } catch (const std::exception&) {
    threw = true;
  }
  if (!threw) {
    ++g_failures;
    std::cerr << "FAIL: " << file << ":" << line << ": expected throw from "
              << what << "\n";
  }
}
#define CHECK_THROWS(fn) ExpectThrow((fn), #fn, __FILE__, __LINE__)

using namespace lark::rpc;

// ─────────────────────────────────────────────────────────────────────────────
// A custom OOP service (business subclass of the interface).
// ─────────────────────────────────────────────────────────────────────────────
class CalculatorService : public RpcService {
 public:
  const std::string& name() const noexcept override { return name_; }

  std::vector<std::string> methods() const override {
    return {"add", "mul"};
  }

  RpcStatus Dispatch(const std::string& method, const RpcMessage& request,
                     RpcMessage& response) override {
    if (method != "add" && method != "mul") {
      return RpcStatus::NotFound("calc has no method '" + method + "'");
    }
    const std::string& body = request.payload;
    const size_t comma = body.find(',');
    if (comma == std::string::npos)
      return RpcStatus::InvalidArgument("expected 'a,b'");
    const int a = std::stoi(body.substr(0, comma));
    const int b = std::stoi(body.substr(comma + 1));
    const int r = method == "add" ? a + b : a * b;
    response = RpcMessage::FromString(std::to_string(r));
    return RpcStatus::Ok();
  }

 private:
  std::string name_ = "calc";
};

void TestStatusAndEndpoint() {
  std::cout << "Test RpcStatus / RpcEndpoint...\n";

  CHECK(RpcStatus::Ok().ok());
  CHECK(!RpcStatus::NotFound("x").ok());
  CHECK(RpcStatus::NotFound("x").code() == RpcCode::kNotFound);
  CHECK(!RpcStatus::Internal("boom").ok());

  RpcEndpoint ep = RpcEndpoint::Parse("inproc://billing");
  CHECK(ep.scheme == "inproc" && ep.address == "billing");
  CHECK(ep.uri() == "inproc://billing");

  RpcEndpoint bare = RpcEndpoint::Parse("orders");
  CHECK(bare.scheme == "inproc" && bare.address == "orders");

  std::cout << "  done\n";
}

void TestServiceImpl() {
  std::cout << "Test RpcServiceImpl...\n";

  auto svc = std::make_shared<RpcServiceImpl>("echo");
  svc->AddMethod("hello", [](const RpcMessage& req, RpcMessage& res) {
    res = RpcMessage::FromString("hi:" + req.ToString());
    return RpcStatus::Ok();
  });
  svc->AddMethod("fail", [](const RpcMessage&, RpcMessage&) {
    return RpcStatus::Internal("boom");
  });

  CHECK(svc->name() == "echo");
  CHECK(svc->methods().size() == 2);

  RpcMessage resp;
  auto st = svc->Dispatch("hello", RpcMessage::FromString("lark"), resp);
  CHECK(st.ok());
  CHECK(resp.ToString() == "hi:lark");

  auto bad = svc->Dispatch("nope", RpcMessage{}, resp);
  CHECK(!bad.ok() && bad.code() == RpcCode::kNotFound);

  auto inner = svc->Dispatch("fail", RpcMessage{}, resp);
  CHECK(!inner.ok() && inner.code() == RpcCode::kInternal);

  std::cout << "  done\n";
}

void TestInProcRpc() {
  std::cout << "Test in-process RPC backend...\n";

  auto backend = lark::rpc::RpcFactory::Instance().Create("inproc");
  CHECK(backend != nullptr);
  CHECK(std::string(backend->name()) == "inproc");

  // server side
  auto server = backend->CreateServer(RpcEndpoint{"inproc", "billing"});
  auto calc = std::make_shared<CalculatorService>();
  server->RegisterService(calc);
  server->Start();

  // client side (created from the same backend factory)
  auto channel = backend->CreateChannel(RpcEndpoint{"inproc", "billing"});

  RpcMessage req = RpcMessage::FromString("20,22");
  RpcMessage resp;
  RpcCallOptions opts(std::chrono::milliseconds(500));

  auto st = channel->Call("calc", "add", req, resp, opts);
  CHECK(st.ok());
  CHECK(resp.ToString() == "42");
  CHECK(resp.ToString() != req.ToString());  // mutated in place

  auto st2 = channel->Call("calc", "mul", req, resp, opts);
  CHECK(st2.ok() && resp.ToString() == "440");

  // unknown method -> not_found; unknown service -> unavailable
  auto st3 = channel->Call("calc", "div", req, resp, opts);
  CHECK(!st3.ok() && st3.code() == RpcCode::kNotFound);

  auto st4 = channel->Call("ghost", "add", req, resp, opts);
  CHECK(!st4.ok() && st4.code() == RpcCode::kUnavailable);

  // headers survive the in-process trip
  auto svc = std::make_shared<RpcServiceImpl>("meta");
  svc->AddMethod("headers", [](const RpcMessage& in, RpcMessage& out) {
    auto it = in.headers.find("trace-id");
    out.set_header("trace-id", it == in.headers.end() ? "none" : it->second);
    return RpcStatus::Ok();
  });
  auto meta_server = backend->CreateServer(RpcEndpoint{"inproc", "meta"});
  meta_server->RegisterService(svc);
  meta_server->Start();
  auto meta_channel = backend->CreateChannel(RpcEndpoint{"inproc", "meta"});
  RpcMessage hreq;
  hreq.set_header("trace-id", "abc");
  RpcMessage hresp;
  CHECK(meta_channel->Call("meta", "headers", hreq, hresp, opts).ok());
  CHECK(hresp.headers["trace-id"] == "abc");

  server->Stop();
  meta_server->Stop();
  // after stop, calls fail with unavailable
  CHECK(!channel->Call("calc", "add", req, resp, opts).ok());

  std::cout << "  done\n";
}

void TestFactory() {
  std::cout << "Test RpcFactory...\n";

  auto& factory = lark::rpc::RpcFactory::Instance();
  CHECK(factory.Contains("inproc"));
  bool saw_inproc = false;
  for (const auto& n : factory.Available())
    if (n == "inproc") saw_inproc = true;
  CHECK(saw_inproc);

  // unknown backend -> throws out_of_range
  CHECK_THROWS([&] { factory.Create("carrier_pigeon"); });

  std::cout << "  done\n";
}

void TestOptionalBackends() {
  std::cout << "Test optional gRPC/brpc backends...\n";

  // Direct instantiation fails gracefully when the backend is not compiled in.
#ifndef LARK_WITH_GRPC
  GrpcBackend grpc;
  CHECK_THROWS([&] { grpc.CreateChannel(RpcEndpoint{"grpc", "x:1"}); });
  CHECK_THROWS([&] { grpc.CreateServer(RpcEndpoint{"grpc", "x:1"}); });
#endif
#ifndef LARK_WITH_BRPC
  BrpcBackend brpc;
  CHECK_THROWS([&] { brpc.CreateChannel(RpcEndpoint{"brpc", "x:1"}); });
#endif

  std::cout << "  done\n";
}

}  // namespace

int main() {
  std::cout << "=== Generic RPC Framework Tests ===\n\n";
  TestStatusAndEndpoint();
  TestServiceImpl();
  TestInProcRpc();
  TestFactory();
  TestOptionalBackends();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks
            << " checks passed\n";
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) FAILED\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
