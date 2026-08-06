#include "dag/node_registry.h"

#include <stdexcept>
#include <utility>

namespace lark {

using std::invalid_argument;
using std::lock_guard;
using std::move;
using std::out_of_range;
using std::runtime_error;

NodeRegistry& NodeRegistry::Instance() {
  static NodeRegistry registry;
  return registry;
}

void NodeRegistry::Register(std::string type_name, Factory factory) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto [it, inserted] = factories_.emplace(std::move(type_name),
                                           std::move(factory));
  if (!inserted) {
    throw std::invalid_argument("NodeRegistry: duplicate node type '" +
                                it->first + "'");
  }
}

bool NodeRegistry::Contains(const std::string& type_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.find(type_name) != factories_.end();
}

std::unique_ptr<Node> NodeRegistry::Create(const std::string& type_name,
                                           const std::string& id) const {
  Factory factory;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(type_name);
    if (it == factories_.end()) {
      throw std::out_of_range("NodeRegistry: unknown node type '" + type_name +
                              "'");
    }
    factory = it->second;
  }
  std::unique_ptr<Node> node = factory();
  node->SetIdentity(id.empty() ? type_name : id, type_name);
  return node;
}

std::vector<std::string> NodeRegistry::RegisteredTypes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> types;
  types.reserve(factories_.size());
  for (const auto& [name, _] : factories_) {
    types.push_back(name);
  }
  return types;
}

}  // namespace lark
