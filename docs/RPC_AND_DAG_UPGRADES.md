# RPC Framework & DAG Upgrades (RPC 框架与 DAG 升级)

> Bilingual / 双语: [English](#english) · [中文](#中文)

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

## 中文

### 1. 通用 RPC 框架（`rpc/`）

基于 OOP 思想的传输无关 RPC 抽象：业务面向接口编程，具体框架通过工厂接入。

**接口（OOP）：**

| 接口 | 职责 |
|------|------|
| `RpcService` | 可被远程调用的服务（`Dispatch`）；`RpcServiceImpl` 按方法名注册 handler |
| `RpcChannel` | 客户端传输抽象（`Call(service, method, req, res, options)`） |
| `RpcServer` | 服务端传输抽象（`RegisterService` / `Start` / `Stop`） |
| `RpcBackend` | 为某种线上格式创建 channel / server |
| `RpcFactory` | 按名注册后端的工厂（策略模式） |

**后端：**

- `inproc` — 内置，进程内直连（无 socket、无序列化依赖），用于测试/本地开发。
- `grpc` / `brpc` — 适配层；仅在 `LARK_WITH_GRPC=ON` / `LARK_WITH_BRPC=ON` 时可用，
  否则优雅失败，调用方代码无需改动。

### 2. DAG 业务执行框架升级（`dag/`）

- **每个 DagNode 提供执行监控耗时能力** — `Node` 记录 `elapsed()` 与 `started_at()`
  （相对运行起点的偏移），便于做瀑布图；内置 `dag::StatsCollector : Monitor`
  开箱即用地聚合：`std::cout << stats.stats().summary();`
- **执行 dag 图时批量关闭节点** — `Executor::Execute(graph, ctx, {"nodeA", "nodeB"})`
  对本次运行跳过指定节点：被跳过的节点状态为 `NodeStatus::kSkipped`，其完成事件
  立即触发，下游照常执行，图必然收敛（不会阻塞），并回调 `Monitor::OnNodeSkipped`。

### Column engine (v4)

See `docs/COLUMN_ENGINE_V4.md` for the feed / compute / fetch engine, including
the refactor that splits the execution-layer ops and the Pipeline compiler into
small focused classes.
