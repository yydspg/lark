# lark_rpc — Generic RPC Framework

A transport-agnostic RPC abstraction built with OOP design: business code
programs against interfaces; concrete frameworks (in-process / gRPC / brpc) plug
in behind a backend factory.

## Interfaces (OOP)

| Interface | Role |
|-----------|------|
| `rpc::RpcService` / `RpcServiceImpl` | remotely callable service (`Dispatch(method, req, res)`); `RpcServiceImpl` builds one from named handlers |
| `rpc::RpcChannel` | client-side transport (`Call(service, method, req, res, options)`) |
| `rpc::RpcServer` | server-side transport (`RegisterService` / `Start` / `Stop`) |
| `rpc::RpcBackend` | creates channels & servers for one wire format |
| `rpc::RpcFactory` | backend registry keyed by name (strategy pattern) |
| `rpc::RpcMessage` / `RpcStatus` / `RpcEndpoint` / `RpcCallOptions` | transport-agnostic data + metadata |

## Backends

- `inproc` — always available; routes calls to services registered in the same
  process (no sockets, no serialization deps). Used by tests and local dev.
- `grpc` / `brpc` — adapters; functional only when built with
  `LARK_WITH_GRPC=ON` / `LARK_WITH_BRPC=ON`. Without the flag they fail
  gracefully, so call sites compile uniformly.

## Quick start

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

## Monitoring

`RpcCallOptions.monitor` — when set, the channel emits a `metric` `"rpc"`
`rpc.call` event per call (service, method, duration, ok).

## Dependencies

- `lark_metric` (monitoring)

## Build / link

```cmake
add_subdirectory(rpc)
target_link_libraries(my_app PRIVATE lark_rpc)
```

See `examples/rpc_example.cpp` and `tests/test_rpc.cpp`.
