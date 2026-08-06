// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "cache/cache.h"

#include <algorithm>
#include <stdexcept>

namespace lark::cache {

namespace {
std::string ToHeaderBool(bool v) { return v ? "1" : "0"; }
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// LocalCache
// ─────────────────────────────────────────────────────────────────────────────
LocalCache::LocalCache(CacheOptions options) : options_(std::move(options)) {}

void LocalCache::Emit(const std::string& action, const std::string& key,
                      bool ok) {
  if (!options_.monitor) return;
  ::lark::monitor::Event e{"cache", action, key};
  e.ok = ok;
  options_.monitor->Emit(e);
}

bool LocalCache::IsExpired(const Entry& e,
                           ::lark::monitor::Clock::time_point now) const {
  return e.has_ttl && now >= e.expires_at;
}

std::optional<std::string> LocalCache::Get(const std::string& key) {
  const auto now = ::lark::monitor::Clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(key);
  if (it == entries_.end()) {
    Emit("cache.miss", key);
    return std::nullopt;
  }
  if (IsExpired(it->second, now)) {
    lru_.remove(key);
    entries_.erase(it);
    Emit("cache.miss", key);
    return std::nullopt;
  }
  // Refresh LRU position.
  lru_.remove(key);
  lru_.push_front(key);
  Emit("cache.hit", key);
  return it->second.value;
}

void LocalCache::Put(const std::string& key, std::string value,
                     std::chrono::milliseconds ttl) {
  const auto now = ::lark::monitor::Clock::now();
  std::lock_guard<std::mutex> lock(mutex_);

  const std::chrono::milliseconds t =
      ttl.count() > 0 ? ttl : options_.default_ttl;

  auto it = entries_.find(key);
  if (it != entries_.end()) {
    // Update in place and refresh LRU position.
    it->second.value = std::move(value);
    it->second.has_ttl = t.count() > 0;
    it->second.expires_at = now + t;
    lru_.remove(key);
    lru_.push_front(key);
    Emit("cache.put", key);
    return;
  }

  // Evict least-recently-used when at capacity.
  if (options_.capacity > 0 && entries_.size() >= options_.capacity) {
    while (!lru_.empty() && entries_.size() >= options_.capacity) {
      const std::string victim = lru_.back();
      lru_.pop_back();
      entries_.erase(victim);
      Emit("cache.evict", victim);
    }
  }

  Entry e;
  e.value = std::move(value);
  e.has_ttl = t.count() > 0;
  e.expires_at = now + t;
  entries_[key] = std::move(e);
  lru_.push_front(key);
  Emit("cache.put", key);
}

bool LocalCache::Remove(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(key);
  if (it == entries_.end()) return false;
  entries_.erase(it);
  lru_.remove(key);
  Emit("cache.remove", key);
  return true;
}

void LocalCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  lru_.clear();
  Emit("cache.clear", "");
}

std::size_t LocalCache::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// CacheRpcService
// ─────────────────────────────────────────────────────────────────────────────
CacheRpcService::CacheRpcService(std::shared_ptr<LocalCache> store,
                                 std::string name)
    : name_(std::move(name)), store_(std::move(store)) {}

std::vector<std::string> CacheRpcService::methods() const {
  return {"get", "put", "remove", "clear", "size"};
}

rpc::RpcStatus CacheRpcService::Dispatch(const std::string& method,
                                         const rpc::RpcMessage& request,
                                         rpc::RpcMessage& response) {
  const std::string* key = [&]() -> const std::string* {
    auto it = request.headers.find("key");
    return it == request.headers.end() ? nullptr : &it->second;
  }();

  if (method == "get") {
    if (!key) return rpc::RpcStatus::InvalidArgument("get: missing header 'key'");
    auto v = store_->Get(*key);
    response.headers["found"] = ToHeaderBool(v.has_value());
    if (v) response.payload = std::move(*v);
    return rpc::RpcStatus::Ok();
  }
  if (method == "put") {
    if (!key) return rpc::RpcStatus::InvalidArgument("put: missing header 'key'");
    std::chrono::milliseconds ttl{0};
    auto it = request.headers.find("ttl_ms");
    if (it != request.headers.end()) ttl = std::chrono::milliseconds(std::stoll(it->second));
    store_->Put(*key, request.payload, ttl);
    return rpc::RpcStatus::Ok();
  }
  if (method == "remove") {
    if (!key) return rpc::RpcStatus::InvalidArgument("remove: missing header 'key'");
    response.headers["found"] = ToHeaderBool(store_->Remove(*key));
    return rpc::RpcStatus::Ok();
  }
  if (method == "clear") {
    store_->Clear();
    return rpc::RpcStatus::Ok();
  }
  if (method == "size") {
    response.payload = std::to_string(store_->size());
    return rpc::RpcStatus::Ok();
  }
  return rpc::RpcStatus::NotFound("cache has no method '" + method + "'");
}

// ─────────────────────────────────────────────────────────────────────────────
// RemoteCache
// ─────────────────────────────────────────────────────────────────────────────
RemoteCache::RemoteCache(CacheOptions options,
                         std::shared_ptr<rpc::RpcChannel> channel,
                         std::string service)
    : options_(std::move(options)),
      channel_(std::move(channel)),
      service_(std::move(service)) {}

void RemoteCache::Emit(const std::string& action, const std::string& key,
                       bool ok) {
  if (!options_.monitor) return;
  ::lark::monitor::Event e{"cache", action, key};
  e.ok = ok;
  options_.monitor->Emit(e);
}

std::chrono::milliseconds RemoteCache::ResolveTtl(std::chrono::milliseconds ttl) const {
  return ttl.count() > 0 ? ttl : options_.default_ttl;
}

std::optional<std::string> RemoteCache::Get(const std::string& key) {
  rpc::RpcMessage req, res;
  req.headers["key"] = key;
  rpc::RpcCallOptions opts(std::chrono::milliseconds(500));
  rpc::RpcStatus st = channel_->Call(service_, "get", req, res, opts);
  if (!st.ok()) {
    Emit("cache.error", key, false);
    return std::nullopt;
  }
  const bool found = res.headers["found"] == "1";
  Emit(found ? "cache.hit" : "cache.miss", key);
  if (!found) return std::nullopt;
  return res.payload;
}

void RemoteCache::Put(const std::string& key, std::string value,
                      std::chrono::milliseconds ttl) {
  rpc::RpcMessage req;
  req.headers["key"] = key;
  req.headers["ttl_ms"] = std::to_string(ResolveTtl(ttl).count());
  req.payload = std::move(value);
  rpc::RpcMessage res;
  rpc::RpcCallOptions opts(std::chrono::milliseconds(500));
  rpc::RpcStatus st = channel_->Call(service_, "put", req, res, opts);
  Emit("cache.put", key, st.ok());
}

bool RemoteCache::Remove(const std::string& key) {
  rpc::RpcMessage req, res;
  req.headers["key"] = key;
  rpc::RpcCallOptions opts(std::chrono::milliseconds(500));
  rpc::RpcStatus st = channel_->Call(service_, "remove", req, res, opts);
  Emit("cache.remove", key, st.ok());
  return st.ok() && res.headers["found"] == "1";
}

void RemoteCache::Clear() {
  rpc::RpcMessage req, res;
  rpc::RpcCallOptions opts(std::chrono::milliseconds(500));
  rpc::RpcStatus st = channel_->Call(service_, "clear", req, res, opts);
  Emit("cache.clear", "", st.ok());
}

std::size_t RemoteCache::size() const {
  rpc::RpcMessage req, res;
  rpc::RpcCallOptions opts(std::chrono::milliseconds(500));
  rpc::RpcStatus st = channel_->Call(service_, "size", req, res, opts);
  if (!st.ok()) return 0;
  try {
    return static_cast<std::size_t>(std::stoull(res.payload));
  } catch (...) {
    return 0;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// CacheFactory
// ─────────────────────────────────────────────────────────────────────────────
CacheFactory::CacheFactory() {
  factories_.emplace("local", [](const CacheOptions& options,
                                 std::shared_ptr<rpc::RpcChannel>,
                                 const std::string&) {
    return std::make_shared<LocalCache>(options);
  });
  factories_.emplace("remote", [](const CacheOptions& options,
                                  std::shared_ptr<rpc::RpcChannel> channel,
                                  const std::string& service) {
    return std::make_shared<RemoteCache>(options, std::move(channel), service);
  });
}

CacheFactory& CacheFactory::Instance() {
  static CacheFactory instance;
  return instance;
}

void CacheFactory::Register(std::string type, CacheFactoryFn factory) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!factories_.emplace(std::move(type), std::move(factory)).second) {
    throw std::invalid_argument("CacheFactory: duplicate cache type");
  }
}

std::shared_ptr<Cache> CacheFactory::Create(
    const std::string& type, const CacheOptions& options,
    std::shared_ptr<rpc::RpcChannel> channel, const std::string& service) const {
  CacheFactoryFn factory;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(type);
    if (it == factories_.end()) {
      throw std::out_of_range("CacheFactory: unknown cache type '" + type + "'");
    }
    factory = it->second;
  }
  return factory(options, std::move(channel), service);
}

bool CacheFactory::Contains(const std::string& type) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.find(type) != factories_.end();
}

std::vector<std::string> CacheFactory::Types() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> types;
  types.reserve(factories_.size());
  for (const auto& [type, _] : factories_) types.push_back(type);
  return types;
}

}  // namespace lark::cache
