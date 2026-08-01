#pragma once

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>

#include "dag/i_context.h"

namespace lark {

using std::shared_ptr;
using std::string;
using std::type_index;

// ---- typed convenience layer (header-only, works on any IContext) --------
template <typename T>
void Set(IContext& ctx, const string& key, T value) {
  auto holder = std::make_shared<T>(std::move(value));
  ctx.SetVoid(key, holder, std::type_index(typeid(T)));
}

template <typename T>
shared_ptr<T> Get(const IContext& ctx, const string& key) {
  return std::const_pointer_cast<T>(
      std::static_pointer_cast<const T>(
          ctx.GetVoid(key, std::type_index(typeid(T)))));
}

template <typename T>
bool Has(const IContext& ctx, const string& key) {
  return ctx.Has(key, std::type_index(typeid(T)));
}

template <typename T>
T& Require(const IContext& ctx, const string& key) {
  if (auto ptr = Get<T>(ctx, key)) {
    return *ptr;
  }
  throw std::out_of_range("IContext::Require: missing key '" + key + "'");
}

template <typename T, typename... Args>
shared_ptr<T> ProvideDomain(IContext& ctx, Args&&... args) {
  auto holder = std::make_shared<T>(std::forward<Args>(args)...);
  ctx.SetDomainVoid(std::type_index(typeid(T)), holder);
  return holder;
}

template <typename T>
shared_ptr<T> Domain(const IContext& ctx) {
  return std::const_pointer_cast<T>(
      std::static_pointer_cast<const T>(
          ctx.GetDomainVoid(std::type_index(typeid(T)))));
}

template <typename T>
T& RequireDomain(const IContext& ctx) {
  if (auto ptr = Domain<T>(ctx)) {
    return *ptr;
  }
  throw std::out_of_range(std::string("IContext::RequireDomain: missing '") +
                          typeid(T).name() + "'");
}

// Convenience alias: the default concrete context shipped with the framework.
// New code may prefer to name DefaultContext explicitly; this alias exists so
// that `Context ctx;` continues to work.
class DefaultContext;
using Context = DefaultContext;

}  // namespace lark
