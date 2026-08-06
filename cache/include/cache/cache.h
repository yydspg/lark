// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Highly-abstract cache module.
//
// Business code programs against the Cache interface; the implementation is
// selected per use case through CacheFactory:
//   * "local"  — thread-safe in-memory TTL cache with LRU eviction
//   * "remote" — thin client wrapper over a cache RPC service (lark_rpc);
//                the server side is provided by CacheRpcService (backed by a
//                LocalCache), so it works end-to-end with the in-process
//                backend and with gRPC/brpc when configured.
//
// Both implementations emit cache.* events into the configured
// lark::monitor::Monitor (also pluggable), so caching is observable end-to-end.

#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "monitor/monitor.h"
#include "rpc/channel.h"
#include "rpc/service.h"
#include "rpc/status.h"

namespace lark::cache {

// ─────────────────────────────────────────────────────────────────────────────
// CacheOptions: per-instance configuration.
// ─────────────────────────────────────────────────────────────────────────────
struct CacheOptions {
  std::size_t capacity = 1024;                       // 0 = unlimited entries
  std::chrono::milliseconds default_ttl{0};          // 0 = no expiry
  std::string name = "cache";
  std::shared_ptr<::lark::monitor::Monitor> monitor;  // pluggable (may be null)

  CacheOptions() = default;
  CacheOptions(std::size_t capacity, std::chrono::milliseconds ttl)
      : capacity(capacity), default_ttl(ttl) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// Cache: the abstract caching interface.
// ─────────────────────────────────────────────────────────────────────────────
class Cache {
 public:
  virtual ~Cache() = default;

  virtual const char* name() const noexcept = 0;  // implementation: "local" / "remote"

  // Fetch a value; nullopt when absent or expired.
  virtual std::optional<std::string> Get(const std::string& key) = 0;
  // Store a value; ttl of 0 uses the instance default (0 = no expiry).
  virtual void Put(const std::string& key, std::string value,
                   std::chrono::milliseconds ttl = std::chrono::milliseconds(0)) = 0;
  // Remove a key; returns whether it existed.
  virtual bool Remove(const std::string& key) = 0;
  virtual void Clear() = 0;
  virtual std::size_t size() const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// LocalCache: thread-safe in-memory TTL cache with LRU eviction.
// ─────────────────────────────────────────────────────────────────────────────
class LocalCache final : public Cache {
 public:
  explicit LocalCache(CacheOptions options);

  const char* name() const noexcept override { return "local"; }
  std::optional<std::string> Get(const std::string& key) override;
  void Put(const std::string& key, std::string value,
           std::chrono::milliseconds ttl = std::chrono::milliseconds(0)) override;
  bool Remove(const std::string& key) override;
  void Clear() override;
  std::size_t size() const override;

 private:
  struct Entry {
    std::string value;
    bool has_ttl = false;
    ::lark::monitor::Clock::time_point expires_at{};
  };

  void Emit(const std::string& action, const std::string& key, bool ok = true);
  bool IsExpired(const Entry& e, ::lark::monitor::Clock::time_point now) const;

  mutable std::mutex mutex_;
  CacheOptions options_;
  std::unordered_map<std::string, Entry> entries_;
  std::list<std::string> lru_;  // most-recently-used at front
};

// ─────────────────────────────────────────────────────────────────────────────
// CacheRpcService: server-side RPC service backed by a LocalCache, so the
// "remote" cache can be served over the RPC framework.
//
// Protocol (RpcMessage):
//   get     req.headers["key"]=k             -> res.headers["found"]=0/1, payload=value
//   put     req.headers["key"]=k, ["ttl_ms"], payload=value
//   remove  req.headers["key"]=k             -> res.headers["found"]=0/1
//   clear   (no args)
//   size    (no args)                        -> payload=size
// ─────────────────────────────────────────────────────────────────────────────
class CacheRpcService final : public rpc::RpcService {
 public:
  explicit CacheRpcService(std::shared_ptr<LocalCache> store,
                           std::string name = "cache");

  const std::string& name() const noexcept override { return name_; }
  std::vector<std::string> methods() const override;
  rpc::RpcStatus Dispatch(const std::string& method, const rpc::RpcMessage& request,
                          rpc::RpcMessage& response) override;

 private:
  std::string name_;
  std::shared_ptr<LocalCache> store_;
};

// ─────────────────────────────────────────────────────────────────────────────
// RemoteCache: a Cache implementation that forwards every operation to a
// remote cache service through an RpcChannel (in-process / gRPC / brpc).
// ─────────────────────────────────────────────────────────────────────────────
class RemoteCache final : public Cache {
 public:
  RemoteCache(CacheOptions options, std::shared_ptr<rpc::RpcChannel> channel,
              std::string service = "cache");

  const char* name() const noexcept override { return "remote"; }
  std::optional<std::string> Get(const std::string& key) override;
  void Put(const std::string& key, std::string value,
           std::chrono::milliseconds ttl = std::chrono::milliseconds(0)) override;
  bool Remove(const std::string& key) override;
  void Clear() override;
  std::size_t size() const override;

 private:
  void Emit(const std::string& action, const std::string& key, bool ok = true);
  std::chrono::milliseconds ResolveTtl(std::chrono::milliseconds ttl) const;

  CacheOptions options_;
  std::shared_ptr<rpc::RpcChannel> channel_;
  std::string service_;
};

// ─────────────────────────────────────────────────────────────────────────────
// CacheFactory: register / select cache implementations by type.
// Registered by default: "local", "remote".
// ─────────────────────────────────────────────────────────────────────────────
using CacheFactoryFn = std::function<std::shared_ptr<Cache>(
    const CacheOptions&, std::shared_ptr<rpc::RpcChannel>, const std::string&)>;

class CacheFactory {
 public:
  static CacheFactory& Instance();

  void Register(std::string type, CacheFactoryFn factory);
  std::shared_ptr<Cache> Create(const std::string& type, const CacheOptions& options,
                                std::shared_ptr<rpc::RpcChannel> channel = {},
                                const std::string& service = "cache") const;
  bool Contains(const std::string& type) const;
  std::vector<std::string> Types() const;

 private:
  CacheFactory();
  mutable std::mutex mutex_;
  std::unordered_map<std::string, CacheFactoryFn> factories_;
};

}  // namespace lark::cache
