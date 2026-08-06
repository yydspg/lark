// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "rpc/backend.h"

#include <stdexcept>
#include <utility>

#include "rpc/inproc_backend.h"

namespace lark::rpc {

RpcServiceImpl::RpcServiceImpl(std::string name) : name_(std::move(name)) {}

RpcServiceImpl& RpcServiceImpl::AddMethod(std::string method,
                                          RpcMethodHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  methods_[std::move(method)] = std::move(handler);
  return *this;
}

std::vector<std::string> RpcServiceImpl::methods() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;
  out.reserve(methods_.size());
  for (const auto& [name, _] : methods_) out.push_back(name);
  return out;
}

RpcStatus RpcServiceImpl::Dispatch(const std::string& method,
                                   const RpcMessage& request,
                                   RpcMessage& response) {
  RpcMethodHandler handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = methods_.find(method);
    if (it == methods_.end()) {
      return RpcStatus::NotFound("service '" + name_ + "' has no method '" +
                                 method + "'");
    }
    handler = it->second;
  }
  return handler(request, response);
}

// ─────────────────────────────────────────────────────────────────────────────
// RpcFactory
// ─────────────────────────────────────────────────────────────────────────────
RpcFactory::RpcFactory() {
  factories_.emplace("inproc", [] { return CreateInProcBackend(); });
#ifdef LARK_WITH_GRPC
  factories_.emplace("grpc", [] { return std::make_unique<GrpcBackend>(); });
#endif
#ifdef LARK_WITH_BRPC
  factories_.emplace("brpc", [] { return std::make_unique<BrpcBackend>(); });
#endif
}

RpcFactory& RpcFactory::Instance() {
  static RpcFactory instance;
  return instance;
}

void RpcFactory::Register(std::string name, RpcBackendFactory factory) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!factories_.emplace(std::move(name), std::move(factory)).second) {
    throw std::invalid_argument("RpcFactory: duplicate backend name");
  }
}

std::unique_ptr<RpcBackend> RpcFactory::Create(const std::string& name) const {
  RpcBackendFactory factory;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(name);
    if (it != factories_.end()) {
      factory = it->second;
      found = true;
    }
  }
  if (!found) {
    std::string list;
    for (const auto& n : Available()) {
      if (!list.empty()) list += ", ";
      list += n;
    }
    throw std::out_of_range("RpcFactory: unknown backend '" + name +
                            "' (available: " + list + ")");
  }
  return factory();
}

bool RpcFactory::Contains(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.find(name) != factories_.end();
}

std::vector<std::string> RpcFactory::Available() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> names;
  names.reserve(factories_.size());
  for (const auto& [name, _] : factories_) names.push_back(name);
  return names;
}

}  // namespace lark::rpc
