# rpc — Generic RPC Framework

Library: `liblark_rpc` · Headers: `rpc/include/rpc/*`

A transport-agnostic RPC abstraction built with OOP: business programs against
interfaces; concrete frameworks (in-process / gRPC / brpc) plug in behind a
backend factory.

## Usage

```cpp
#include "rpc/rpc.h"
using namespace lark::rpc;

auto backend = RpcFactory::Instance().Create("inproc");

// server side
auto server = backend->CreateServer(RpcEndpoint{"inproc", "greeter"});
server->RegisterService(std::make_shared<RpcServiceImpl>("greeter")
    ->AddMethod("greet", [](const RpcMessage& req, RpcMessage& res) {
        res = RpcMessage::FromString("hello, " + req.ToString() + "!");
        return RpcStatus::Ok();
    }));
server->Start();

// client side
auto channel = backend->CreateChannel(RpcEndpoint{"inproc", "greeter"});
RpcMessage res;
RpcStatus st = channel->Call("greeter", "greet",
                             RpcMessage::FromString("lark"), res, {500ms});
if (st.ok()) std::cout << res.ToString() << "\n";
```

Services may also subclass `RpcService` directly (a `Dispatch` switch) instead
of using `RpcServiceImpl`. Endpoints are `"scheme://address"`; a bare address
defaults to the in-process scheme.

## Caveats

- **`inproc` is the only backend available by default.** `grpc` / `brpc` are
  adapters that throw "built without LARK_WITH_GRPC/LARK_WITH_BRPC" unless the
  build is configured with those flags (and the libraries linked).
- **In-process service lifetime**: an `InProcServer` registers its services into
  a process-wide registry on `Start()` and unregisters on `Stop()`. A call to a
  stopped/never-started service returns `kUnavailable`. Registering the same
  service name twice throws.
- **`RpcServiceImpl::Dispatch`** locks briefly to look up the handler, then
  releases before calling it — handlers may re-enter the service.
- **Monitoring** is per-call: set `RpcCallOptions.monitor` to receive a
  `"rpc"` `rpc.call` event (service, method, duration, ok).
- The wire format is opaque bytes (`RpcMessage.payload`); concrete backends
  translate to/from it at the adapter boundary. The in-process backend uses
  headers + payload directly — define a stable protocol (as `CacheRpcService`
  does).

## Implementation

- **`RpcMessage`**: `payload` bytes + `headers` map. Transport-agnostic.
- **`RpcStatus`**: `RpcCode` (ok/deadline/not_found/unavailable/
  invalid_argument/internal) + message.
- **`RpcServiceImpl`**: name + `unordered_map<method, handler>` guarded by a
  mutex; `Dispatch` looks up the handler and invokes it.
- **In-process backend**: a process-wide `InProcRegistry` maps service names to
  `RpcService*`. `InProcChannel::Call` resolves the service, times the call and
  emits an `rpc.call` event; `InProcServer::Start/Stop` registers/unregisters
  its services. No sockets, no serialization.
- **`RpcFactory`**: name → backend factory (strategy pattern). Defaults:
  `inproc` always; `grpc`/`brpc` registered only under `LARK_WITH_*`.

## Architecture

```
business code
   │  RpcService / RpcChannel / RpcServer   (interfaces)
   ├─ RpcFactory ──▶ inproc backend (always) ──▶ InProcRegistry
   ├─ RpcFactory ──▶ grpc backend  (LARK_WITH_GRPC)
   └─ RpcFactory ──▶ brpc backend  (LARK_WITH_BRPC)
```

`rpc` depends on `metric` (monitoring). `cache` uses it to implement the remote
cache.
