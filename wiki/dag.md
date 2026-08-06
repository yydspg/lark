# dag — DAG Business Execution Framework

Library: `liblark_dag` · Headers: `dag/include/dag/*`

Two layers:

- **Node layer** (classic): `Node` / `Graph` / `GraphBuilder` / `NodeRegistry`
  / `Executor` — synchronous `Compute(ctx)` nodes, auto-registration.
- **Op layer** (business-oriented + coroutine, recommended): `Op` / `OpAspect`
  / `OpGraph` / `OpExecutor` / `OpRegistry` / `OpGraphBuilder` (DSL + KV).

## Usage

### Op layer (recommended)

```cpp
#include "dag/op.h"
#include "dag/op_executor.h"
#include "dag/op_graph.h"
#include "dag/op_graph_builder.h"
#include "dag/op_registry.h"

class FetchUser : public lark::dag::Op {
 public:
  const std::string& Name() const noexcept override {
    static const std::string n = "fetch_user"; return n;
  }
  lark::coro::Task<void> Execute(lark::dag::Context& data) override {
    data.Set("user", co_await LoadUser(data.Get<int>("user_id")));
  }
};
LARK_OP("FetchUser", FetchUser);      // auto-collected (Spring-IOC style)

class SkipExpensive : public lark::dag::OpAspect {   // aspect
 public:
  bool ShouldSkip(const lark::dag::Op& op, const lark::dag::Context& data) override {
    return data.Has<bool>("cheap_mode") && data.Get<bool>("cheap_mode");
  }
};

lark::coro::Pools pools;
lark::dag::OpExecutor executor(pools.Get(lark::coro::PoolKind::kDag), monitor);

// declarative build — KV form
auto graph = lark::dag::OpGraphBuilder{}
    .AddAspect(std::make_shared<SkipExpensive>())
    .Build({ {"FetchUser", {}, "fetch"}, {"ScoreUser", {"fetch"}, "score"} });

// ... or the arrow DSL
auto graph2 = lark::dag::OpGraphBuilder{}.BuildDsl(
    "fetch:FetchUser -> score:ScoreUser\n"
    "threshold:ConstOp -> score:ScoreUser");      // fan-in by reusing the id

lark::dag::Context data;
data.Set("user_id", 7);
executor.Execute(*graph, data);                    // all ops async on the dag pool
```

### Node layer (classic)

```cpp
class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override { lark::Set(ctx, "user", Fetch(...)); }
};
LARK_NODE("fetch_user", FetchUserNode);

lark::GraphBuilder builder;
auto graph = builder.Build({{"fetch_user"}, {"rank", {"fetch_user"}, "rank"}});
lark::DefaultContext ctx;
lark::Executor executor;
executor.SetMonitor(std::make_shared<lark::StatsCollector>(stats));
executor.Execute(*graph, ctx, {"rank"});           // batch-disable "rank" this run
```

## Caveats

- **Aspect skip vs op-internal skip**: `OpAspect::ShouldSkip` is cross-cutting —
  it applies to every op uniformly without touching the op. Op-internal
  conditional logic is written inside `Execute`. Choose per use case.
- **Failed ops do not poison dependents**: a failing op is recorded (status
  `kFailed`) and the graph still drains; dependents run and can observe the
  failure via `executor.Status(op)` / `AllSucceeded()`. If you want fail-fast,
  check statuses after `Execute`.
- **`Context` is lock-free by convention** — distinct fields for concurrent ops.
- **Non-blocking `ExecuteAsync`** captures graph/context by reference: keep them
  alive until the returned `Future` resolves.
- **`depends_on` only adds an ordering edge** (no data flow). To pass data, one
  op must write a field the other reads.
- **DSL**: `id:Type` (bare `Type` = id), `->` chains, `;`/newline separate
  statements, `#` comments; reusing an id fans in its deps; an id with two
  types is an error.
- The **Node layer** and **Op layer** are independent; do not mix nodes and ops
  in one graph.

## Implementation

### Node layer

- `GraphBuilder` materializes registered node types into a `Graph`, wires
  dependencies by id, then runs an iterative three-color DFS cycle check.
- `Executor::SpawnNode` is a `FireAndForget` coroutine: hop onto the IO pool,
  `co_await` every dependency's `AsyncEvent`, run `node.Run(ctx)`, record
  elapsed/status, set the completion event. Batch-disable nodes get
  `NodeStatus::kSkipped` and their event fires immediately (the graph always
  drains).
- Monitoring is unified: the executor emits `metric` `"dag"` events
  (`node.start/success/failure/fallback/skipped`); `dag::StatsCollector` is a
  `metric::Monitor` implementation that rebuilds the domain `ExecutionStats`.

### Op layer

- `OpGraph::Compile` runs Kahn's topological sort with cycle detection.
- `OpExecutor::ExecuteAsync` builds one `coro::Future<void>` per op: each
  op's future = `AllOf(deps)` `ThenAsync` run-op; `RunOp` hops onto the dag
  pool, runs aspects (`ShouldSkip` → `kSkipped`), `OnBefore`, `op->Execute`,
  `OnAfter`, and emits `metric` `"dag"` `op.*` events. Op failures are caught
  so the future still resolves (graph drains).
- `OpRegistry` + `LARK_OP` register op factories by type name at static init.
- `OpGraphBuilder` (KV + arrow DSL) is parsed with the shared
  `toolkit::dsl` framework and instantiates ops via the registry.

## Architecture

```
dag ──depends──▶ coro (pools + Future) , metric (monitor) , toolkit (dsl/time)
```

The classic Node layer is built on `coro::FireAndForget` + `AsyncEvent`; the Op
layer composes `coro::Future` chains on a dedicated dag pool. Both report into
the unified `metric::Monitor`. The Op layer keeps business code away from
framework details (no `NodeStatus`/`Fallback`/pools in business code).
