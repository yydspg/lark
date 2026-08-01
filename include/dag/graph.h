#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dag/node.h"

namespace lark {

using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

// An immutable, validated directed acyclic graph of node instances.
//
// The Graph owns every node through unique_ptr, so the whole graph (and all
// per-node framework state) is released deterministically when the Graph is
// destroyed. Only GraphBuilder constructs a Graph.
class Graph {
 public:
  Graph() = default;
  Graph(const Graph&) = delete;
  Graph& operator=(const Graph&) = delete;
  Graph(Graph&&) = default;
  Graph& operator=(Graph&&) = default;

  const vector<unique_ptr<Node>>& nodes() const noexcept;
  std::size_t size() const noexcept;
  bool empty() const noexcept;

  // Look up a node by its unique id; nullptr if absent.
  Node* Find(const string& id) const;

  // Clear per-run state on all nodes so the graph can be executed again.
  void ResetRunState();

 private:
  friend class GraphBuilder;

  vector<unique_ptr<Node>> nodes_;
  unordered_map<string, Node*> by_id_;
};

}  // namespace lark
