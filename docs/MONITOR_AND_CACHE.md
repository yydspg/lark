# Monitor & Cache Modules

## Monitor (`monitor/` → liblark_monitor)

A unified, **pluggable monitoring abstraction**. Every module (dag, column,
rpc, coro, cache) reports through the same generic interface, and business code
selects the concrete implementation per environment:

```cpp
auto mon = monitor::MonitorFactory::Instance().Create("stats");  // or "logging" / "null" / "composite"
```

- `Event` — generic observation: `source` ("dag"/"column"/"rpc"/"coro"/"cache"),
  `action` ("node.success", "cache.hit", "feed.end", "rpc.call", ...), `subject`,
  free-form `attrs`, `duration`, `ok`.
- `Monitor` — the abstract interface (`Emit(const Event&)`).
- Implementations: `NullMonitor`, `LoggingMonitor`, `StatsMonitor` (aggregate +
  summary), `CompositeMonitor` (fan-out to several monitors).
- `MonitorFactory` — register / select by name ("null", "logging", "stats",
  "composite").

**Pluggability**: the framework only ever emits events; the concrete monitor is
a strategy chosen by the business (dev logging, prod stats, metrics exporter,
or nothing).

## Cache (`cache/` → liblark_cache)

A **highly-abstract cache** — business code programs against the `Cache`
interface; the implementation is selected per use case through `CacheFactory`:

| type     | implementation |
|----------|----------------|
| `local`  | thread-safe in-memory TTL cache with LRU eviction |
| `remote` | RPC-backed cache served by `CacheRpcService` (lark_rpc) |

```cpp
auto local  = cache::CacheFactory::Instance().Create("local",  opts);
auto remote = cache::CacheFactory::Instance().Create("remote", opts, channel, "cache");
local->Put("user:1", "{\"name\":\"alice\"}");
auto v = remote->Get("user:2");
```

- `Cache` — abstract interface: `Get` / `Put(key, value, ttl)` / `Remove` /
  `Clear` / `size`.
- `LocalCache` — TTL expiry + LRU eviction, thread-safe.
- `RemoteCache` — thin client over any `RpcChannel` (in-process / gRPC / brpc);
  the server side is `CacheRpcService` backed by a `LocalCache`, so it works
  end-to-end today with the in-process backend.
- `CacheFactory` — select the implementation by type; new types can be
  registered.

Both implementations emit `cache.hit` / `cache.miss` / `cache.put` /
`cache.evict` events into the configured `monitor::Monitor`, so caching is
observable end-to-end alongside dag / column / rpc / coro events.

See `examples/cache_example.cpp` and `tests/test_cache.cpp`.
