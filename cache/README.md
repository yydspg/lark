# lark_cache — Abstract Cache Module

A highly-abstract cache. Business code programs against the `Cache` interface;
the concrete implementation is selected per use case through `CacheFactory`
("local" / "remote"). Both emit `cache.*` events into the configured
`metric::Monitor`, so caching is observable end-to-end.

## Implementations

| Type | Description |
|------|-------------|
| `local` | thread-safe in-memory TTL cache with LRU eviction |
| `remote` | RPC-backed cache client; served by `CacheRpcService` over `lark_rpc` (works end-to-end with the in-process backend, and with gRPC/brpc when configured) |

## Interface

```cpp
class Cache {
 public:
  virtual std::optional<std::string> Get(const std::string& key) = 0;
  virtual void Put(const std::string& key, std::string value,
                   std::chrono::milliseconds ttl = std::chrono::milliseconds(0)) = 0;
  virtual bool Remove(const std::string& key) = 0;
  virtual void Clear() = 0;
  virtual std::size_t size() const = 0;
};
```

`CacheOptions` configures capacity, default TTL, and the pluggable monitor.

## Quick start

```cpp
#include "cache/cache.h"
using namespace lark;

auto monitor = metric::MonitorFactory::Instance().Create("stats");

// local cache
cache::CacheOptions opts;
opts.monitor = monitor;
auto local = cache::CacheFactory::Instance().Create("local", opts);
local->Put("user:1", "{\"name\":\"alice\"}");
if (auto v = local->Get("user:1")) { /* hit */ }

// remote cache over the RPC framework
auto rpc_backend = rpc::RpcFactory::Instance().Create("inproc");
auto server = rpc_backend->CreateServer(rpc::RpcEndpoint{"inproc", "cache"});
server->RegisterService(std::make_shared<cache::CacheRpcService>(
    std::make_shared<cache::LocalCache>(cache::CacheOptions{})));
server->Start();
std::shared_ptr<rpc::RpcChannel> channel =
    rpc_backend->CreateChannel(rpc::RpcEndpoint{"inproc", "cache"});
auto remote = cache::CacheFactory::Instance().Create("remote", opts, channel, "cache");
remote->Put("user:2", "{\"name\":\"bob\"}");
auto v = remote->Get("user:2");
```

## Monitoring

`cache.hit` / `cache.miss` / `cache.put` / `cache.evict` / `cache.error` events
are emitted into the `CacheOptions.monitor` (any `metric::Monitor`).

## Dependencies

- `lark_metric` (monitoring)
- `lark_rpc` (remote cache transport)

## Build / link

```cmake
add_subdirectory(cache)
target_link_libraries(my_app PRIVATE lark_cache)
```

See `examples/cache_example.cpp` and `tests/test_cache.cpp`.
