#include "dag/graph_builder.h"

#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "dag/graph.h"
#include "dag/node.h"

namespace lark {

using std::invalid_argument;
using std::move;
using std::runtime_error;
using std::size_t;
using std::unordered_map;

// ---------------------------------------------------------------------------
// NodeDef
// ---------------------------------------------------------------------------
NodeDef::NodeDef(string type, vector<string> deps, string id)
    : type(move(type)),
      deps(move(deps)),
      id(this->type.empty() ? string{} : move(id)) {}

// ---------------------------------------------------------------------------
// GraphBuilder
// ---------------------------------------------------------------------------
GraphBuilder::GraphBuilder(const NodeRegistry& registry) : registry_(registry) {}

std::unique_ptr<Graph> GraphBuilder::Build(initializer_list<NodeDef> defs) const {
  return Build(vector<NodeDef>(defs));
}

// ---------------------------------------------------------------------------
// Build: orchestrates the three pipeline stages.
// ---------------------------------------------------------------------------
std::unique_ptr<Graph> GraphBuilder::Build(
    const std::vector<NodeDef>& defs) const {
  auto graph = std::make_unique<Graph>();
  graph->nodes_.reserve(defs.size());
  
  CreateAndIndexNodes(defs, *graph);
  WireDependencies(defs, *graph);
  RejectCycles(*graph);

  return graph;
}

// ---------------------------------------------------------------------------
// Pass 1: materialize node instances from the registry and index them by id.
// ---------------------------------------------------------------------------
void GraphBuilder::CreateAndIndexNodes(const std::vector<NodeDef>& defs,
                                       Graph& graph) const {
  for (const auto& def : defs) {
    if (def.type.empty()) {
      throw std::invalid_argument("GraphBuilder: node def has empty type");
    }
    const std::string id = def.id.empty() ? def.type : def.id;
    std::unique_ptr<Node> node = registry_.Create(def.type, id);
    Node* raw = node.get();
    auto [it, inserted] = graph.by_id_.emplace(id, raw);
    if (!inserted) {
      throw std::invalid_argument("GraphBuilder: duplicate node id '" + id +
                                  "'");
    }
    graph.nodes_.push_back(std::move(node));
  }
}

// ---------------------------------------------------------------------------
// Pass 2: resolve dependency ids to Node* pointers and attach them.
// ---------------------------------------------------------------------------
void GraphBuilder::WireDependencies(const std::vector<NodeDef>& defs,
                                    Graph& graph) const {
  for (std::size_t i = 0; i < defs.size(); ++i) {
    Node* node = graph.nodes_[i].get();
    for (const std::string& dep_id : defs[i].deps) {
      Node* dep = graph.Find(dep_id);
      if (dep == nullptr) {
        throw std::invalid_argument("GraphBuilder: node '" + node->id() +
                                    "' depends on unknown id '" + dep_id + "'");
      }
      if (dep == node) {
        throw std::runtime_error("GraphBuilder: node '" + node->id() +
                                 "' depends on itself");
      }
      node->AddDependency(dep);
    }
  }
}

// ---------------------------------------------------------------------------
// Pass 3: detect cycles with an iterative DFS + three-color marking.
// ---------------------------------------------------------------------------
void GraphBuilder::RejectCycles(const Graph& graph) const {
  // inline class
  enum class Mark { kUnvisited, kInStack, kDone };
  std::unordered_map<const Node*, Mark> marks;
  marks.reserve(graph.size());

  for (const auto& start : graph.nodes()) {
    if (marks[start.get()] != Mark::kUnvisited) {
      continue;
    }
    // Iterative DFS avoids stack overflow on deep graphs.
    std::vector<std::pair<const Node*, std::size_t>> stack;
    stack.emplace_back(start.get(), 0);
    marks[start.get()] = Mark::kInStack;

    while (!stack.empty()) {
      auto& [node, index] = stack.back();
      const auto& deps = node->dependencies();
      if (index < deps.size()) {
        const Node* next = deps[index++];
        Mark& mark = marks[next];
        if (mark == Mark::kInStack) {
          throw std::runtime_error(
              "GraphBuilder: dependency cycle detected involving node '" +
              next->id() + "'");
        }
        if (mark == Mark::kUnvisited) {
          mark = Mark::kInStack;
          stack.emplace_back(next, 0);
        }
      } else {
        marks[node] = Mark::kDone;
        stack.pop_back();
      }
    }
  }
}

}  // namespace lark
