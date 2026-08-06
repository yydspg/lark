# cache — Abstract Cache Module

Library: `liblark_cache` · Headers: `cache/include/cache/*`

A highly-abstract cache. Business programs against the `Cache` interface; the
concrete implementation is chosen per use case through `CacheFactory`
("local" / "remote"). Both emit `cache.*` events into a pluggable
`metric::Monitor`.

## Usage

```cpp
#include "cache/cache.h"
using namespace lark;

auto monitor = metric::MonitorFactory::Instance().Create("stats");

cache::CacheOptions opts;
opts.capacity = 1024;
opts.default_ttl = std::chrono::milliseconds(60000);
opts.monitor = monitor;

// local cache
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

## Caveats

- **Values are byte strings** (`std::string`). Serialize structured data at the
  call site.
- **TTL**: `Put(key, value, ttl)` — a `0` ttl falls back to `default_ttl`; a
  `0` default means "no expiry". Expired entries behave as misses and are
  removed lazily on access.
- **LRU eviction** only happens at `capacity` (0 = unlimited); `Get`/`Put`
  refresh recency. Eviction order is approximate (list-based LRU).
- **Remote cache needs a live server**: `Get`/`Put` become `rpc.call`s; if the
  server is down the call returns `kUnavailable`, `Get` yields a miss and a
  `cache.error` event is emitted. `size()` returns 0 on error.
- **In-process registration is process-global**: `InProcServer` registers the
  service name into `InProcRegistry`; duplicate names throw.
- **Monitoring** emits `cache.hit / miss / put / evict / remove / clear /
  error`.

## Implementation

- **`Cache`**: abstract `Get / Put / Remove / Clear / size`.
- **`LocalCache`**: `unordered_map<key, Entry{value, has_ttl, expires_at}>` +
  an `std::list` for recency, guarded by one mutex. `Get` checks expiry, moves
  the key to the front; `Put` evicts the list tail at capacity. Emits events
  into `options_.monitor`.
- **`CacheRpcService`**: an `rpc::RpcService` over a `LocalCache`. Protocol:
  `get`/`put`/`remove`/`clear`/`size` with key/ttl in headers, value in payload,
  a `found` header on `get`/`remove`.
- **`RemoteCache`**: wraps an `rpc::RpcChannel` + service name; every operation
  is an `rpc.call`. It resolves TTL, translates `RpcStatus`/headers back to
  optional values.
- **`CacheFactory`**: name → creator; `local` ignores the channel, `remote`
  requires it. New implementations can be `Register`ed.

## Architecture

```
business ──Cache (abstract)──▶ CacheFactory
   ├─ "local"  ──▶ LocalCache        (in-memory TTL + LRU)
   └─ "remote" ──▶ RemoteCache ──rpc──▶ CacheRpcService ──▶ LocalCache (server)
                    (client wrapper)       (service)          (authoritative store)
```

`cache` depends on `metric` (monitoring) + `rpc` (remote transport).
