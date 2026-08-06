# RPC Framework & DAG Upgrades


## English

Two upgrades shipped on top of the DAG framework and column engine.

### 1. Generic RPC framework (`rpc/`)

A transport-agnostic RPC abstraction implemented with OOP design: business code
programs against interfaces; concrete frameworks plug in behind a factory.

**Interfaces (OOP):**

| Interface | Role |
|-----------|------|
| `RpcService` | remotely callable service (`Dispatch(method, req, res)`); `RpcServiceImpl` builds one from named handlers |
| `RpcChannel` | client-side transport (`Call(service, method, req, res, options)`) |
| `RpcServer` | server-side transport (`RegisterService` / `Start` / `Stop`) |
| `RpcBackend` | creates channels & servers for one wire format |
| `RpcFactory` | backend registry keyed by name (strategy pattern) |

**Backends:**

- `inproc` — always available; routes calls to services registered in the same
  process (no sockets, no serialization deps). Used by tests / local dev.
- `grpc` / `brpc` — adapters; functional only when built with
  `LARK_WITH_GRPC=ON` / `LARK_WITH_BRPC=ON`. Without the flag they fail
  gracefully, so call sites compile uniformly.

**Example**

```cpp
auto backend = RpcFactory::Instance().Create("inproc");
auto server = backend->CreateServer(RpcEndpoint{"inproc", "greeter"});
server->RegisterService(std::make_shared<GreeterService>());
server->Start();

auto channel = backend->CreateChannel(RpcEndpoint{"inproc", "greeter"});
RpcMessage res;
RpcStatus st = channel->Call("greeter", "greet", RpcMessage::FromString("lark"), res, {500ms});
```

See `examples/rpc_example.cpp` and `tests/test_rpc.cpp`.

### 2. DAG framework upgrade (`dag/`)

- **Per-node monitoring / timing** — every `Node` records `elapsed()` and
  `started_at()` (offset from run start) so waterfall charts are trivial. The
  built-in `dag::StatsCollector : Monitor` aggregates them out of the box:
  `std::cout << stats.stats().summary();`
- **Batch-disable nodes** — `Executor::Execute(graph, ctx, {"nodeA", "nodeB"})`
  skips the listed nodes for that run: each gets `NodeStatus::kSkipped`, its
  completion event fires immediately, and downstream nodes proceed. The graph
  always drains (never blocks), and `Monitor::OnNodeSkipped` is invoked.
