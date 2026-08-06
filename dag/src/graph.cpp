#include "dag/graph.h"

namespace lark {

using std::move;
using std::swap;

const vector<unique_ptr<Node>>& Graph::nodes() const noexcept {
  return nodes_;
}

std::size_t Graph::size() const noexcept {
  return nodes_.size();
}

bool Graph::empty() const noexcept {
  return nodes_.empty();
}

Node* Graph::Find(const std::string& id) const {
  auto it = by_id_.find(id);
  return it == by_id_.end() ? nullptr : it->second;
}

void Graph::ResetRunState() {
  for (auto& node : nodes_) {
    node->ResetRunState();
  }
}

}  // namespace lark
