#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dag/node.h"

namespace lark {

using std::function;
using std::mutex;
using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

// A registry of business node factories keyed by a stable type name.
//
// Business nodes are "collected" into a registry (typically the process-wide
// Instance()). The GraphBuilder then selects nodes by name when materializing
// a graph from key/value definitions.
//
// Auto-registration:
//   Use LARK_NODE(name) macro in your node's .cpp file to automatically
//   register it at program startup. No manual registration calls needed.
//
//   Example:
//     class FetchUserNode : public lark::Node { ... };
//     LARK_NODE("fetch_user", FetchUserNode);  // auto-registered!
//
class NodeRegistry {
 public:
  using Factory = std::function<std::unique_ptr<Node>()>;

  // Process-wide registry used by the LARK_NODE macro. Tests may also create
  // standalone NodeRegistry instances for isolation.
  static NodeRegistry& Instance();

  // Register a factory under `type_name`. Throws std::invalid_argument on a
  // duplicate name so wiring mistakes surface early.
  void Register(std::string type_name, Factory factory);

  bool Contains(const std::string& type_name) const;

  // Create a fresh node of `type_name`, tagged with `id` (defaults to the type
  // name). Throws std::out_of_range if the type is unknown.
  std::unique_ptr<Node> Create(const std::string& type_name,
                               const std::string& id) const;

  std::vector<std::string> RegisteredTypes() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Factory> factories_;
};

// ---------------------------------------------------------------------------
// Auto-registration macros (Spring-like)
// ---------------------------------------------------------------------------

// LARK_NODE(name, class)
//   Registers `class` under `name` in the process-wide registry at static-init
//   time. Place at namespace scope in the node's .cpp file:
//
//     class FetchUserNode : public lark::Node {
//       void Compute(lark::IContext& ctx) override { ... }
//     };
//
//     LARK_NODE("fetch_user", FetchUserNode);
//
//   The node is automatically registered when the program starts. No manual
//   registration calls needed.
//
#define LARK_NODE(NAME, CLASS)                                              \
  namespace {                                                               \
  [[maybe_unused]] const bool kLarkNodeRegistered_##CLASS = [] {           \
    ::lark::NodeRegistry::Instance().Register(                              \
        (NAME), [] { return std::make_unique<CLASS>(); });                 \
    return true;                                                            \
  }();                                                                      \
  }

// Backward compatibility: old macro name
#define DAG_REGISTER_NODE(NAME, CLASS) LARK_NODE(NAME, CLASS)

}  // namespace lark
