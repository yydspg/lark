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
T& Get(const IContext& ctx, const string& key) {
  if (auto ptr = std::const_pointer_cast<T>(
          std::static_pointer_cast<const T>(
              ctx.GetVoid(key, std::type_index(typeid(T)))))) {
    return *ptr;
  }
  throw std::out_of_range("IContext::Get: missing key '" + key + "'");
}

template <typename T>
bool Has(const IContext& ctx, const string& key) {
  return ctx.Has(key, std::type_index(typeid(T)));
}

template <typename T>
T& Require(const IContext& ctx, const string& key) {
  return Get<T>(ctx, key);
}

template <typename T, typename... Args>
shared_ptr<T> ProvideDomain(IContext& ctx, Args&&... args) {
  auto holder = std::make_shared<T>(std::forward<Args>(args)...);
  ctx.SetDomainVoid(std::type_index(typeid(T)), holder);
  return holder;
}

template <typename T>
T& Domain(const IContext& ctx) {
  if (auto ptr = std::const_pointer_cast<T>(
          std::static_pointer_cast<const T>(
              ctx.GetDomainVoid(std::type_index(typeid(T)))))) {
    return *ptr;
  }
  throw std::out_of_range(std::string("IContext::Domain: missing '") +
                          typeid(T).name() + "'");
}

template <typename T>
T& RequireDomain(const IContext& ctx) {
  return Domain<T>(ctx);
}

// Convenience alias: the default concrete context shipped with the framework.
// New code may prefer to name DefaultContext explicitly; this alias exists so
// that `Context ctx;` continues to work.
class DefaultContext;
using Context = DefaultContext;

}  // namespace lark
