// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "rpc/inproc_backend.h"

#include <stdexcept>
#include <utility>

namespace lark::rpc {

InProcRegistry& InProcRegistry::Instance() {
  static InProcRegistry registry;
  return registry;
}

void InProcRegistry::Register(const std::string& name, RpcService* service) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto [it, inserted] = services_.emplace(name, service);
  if (!inserted) {
    throw std::invalid_argument("InProcRegistry: duplicate service '" + name +
                                "'");
  }
}

void InProcRegistry::Unregister(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  services_.erase(name);
}

RpcService* InProcRegistry::Find(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = services_.find(name);
  return it == services_.end() ? nullptr : it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel
// ─────────────────────────────────────────────────────────────────────────────
InProcChannel::InProcChannel(RpcEndpoint endpoint)
    : endpoint_(std::move(endpoint)) {}

RpcStatus InProcChannel::Call(const std::string& service,
                              const std::string& method,
                              const RpcMessage& request, RpcMessage& response,
                              const RpcCallOptions& options) {
  (void)options;  // in-process dispatch is synchronous; timeout is a no-op
  const auto start = ::lark::monitor::Clock::now();
  RpcService* svc = InProcRegistry::Instance().Find(service);
  if (svc == nullptr) {
    return Finish(options, service, method, start,
                  RpcStatus::Unavailable("inproc: no service '" + service +
                                         "' registered (endpoint " +
                                         endpoint_.uri() + ")"));
  }
  return Finish(options, service, method, start,
                svc->Dispatch(method, request, response));
}

// Emit an "rpc.call" monitoring event (when a monitor is attached).
RpcStatus InProcChannel::Finish(const RpcCallOptions& options,
                                const std::string& service,
                                const std::string& method,
                                ::lark::monitor::Clock::time_point start,
                                RpcStatus status) {
  if (options.monitor) {
    ::lark::monitor::Event e{"rpc", "rpc.call", method};
    e.attr("service", service).attr("endpoint", endpoint_.uri());
    e.duration = ::lark::monitor::Clock::now() - start;
    e.ok = status.ok();
    options.monitor->Emit(e);
  }
  return status;
}

// ─────────────────────────────────────────────────────────────────────────────
// Server
// ─────────────────────────────────────────────────────────────────────────────
InProcServer::InProcServer(RpcEndpoint endpoint) : endpoint_(std::move(endpoint)) {}

InProcServer::~InProcServer() { Stop(); }

void InProcServer::RegisterService(std::shared_ptr<RpcService> service) {
  if (service == nullptr)
    throw std::invalid_argument("InProcServer: null service");
  services_.push_back(std::move(service));
}

void InProcServer::Start() {
  if (started_) return;
  for (const auto& svc : services_) {
    InProcRegistry::Instance().Register(svc->name(), svc.get());
  }
  started_ = true;
}

void InProcServer::Stop() {
  if (!started_) return;
  for (const auto& svc : services_) {
    InProcRegistry::Instance().Unregister(svc->name());
  }
  started_ = false;
}

std::unique_ptr<RpcChannel> InProcBackend::CreateChannel(
    const RpcEndpoint& endpoint) {
  return std::make_unique<InProcChannel>(endpoint);
}

std::unique_ptr<RpcServer> InProcBackend::CreateServer(
    const RpcEndpoint& endpoint) {
  return std::make_unique<InProcServer>(endpoint);
}

std::unique_ptr<RpcBackend> CreateInProcBackend() {
  return std::make_unique<InProcBackend>();
}

}  // namespace lark::rpc
