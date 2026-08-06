// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "dag/op_graph_builder.h"

#include "toolkit/dsl.h"

#include <cctype>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace lark::dag {

std::shared_ptr<OpGraph> OpGraphBuilder::Build(
    const std::vector<OpDef>& defs) const {
  auto graph = std::make_shared<OpGraph>();
  for (const auto& aspect : aspects_) graph->AddAspect(aspect);

  std::unordered_map<std::string, Op*> by_id;
  by_id.reserve(defs.size());

  for (const auto& def : defs) {
    if (def.type.empty()) {
      throw std::invalid_argument("OpGraphBuilder: node def has empty type");
    }
    const std::string id = def.id.empty() ? def.type : def.id;
    auto op = registry_.Create(def.type, id);
    Op* raw = op.get();
    graph->AddOp(std::move(op), id);
    by_id[id] = raw;
  }

  for (const auto& def : defs) {
    const std::string id = def.id.empty() ? def.type : def.id;
    for (const auto& dep : def.deps) {
      if (by_id.find(dep) == by_id.end()) {
        throw std::invalid_argument("OpGraphBuilder: node '" + id +
                                    "' depends on unknown id '" + dep + "'");
      }
      graph->DependsOn(dep, id);
    }
  }
  return graph;
}

// ─────────────────────────────────────────────────────────────────────────────
// DSL (shared toolkit::dsl framework):  seg = id [':' Type]
// chain = seg ('->' seg)* ; statements separated by ';' or a new line.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
// Parse one segment: `id` or `id:Type`.
std::pair<std::string, std::string> Segment(lark::toolkit::dsl::Parser& parser) {
  const auto id = parser.ExpectToken(lark::toolkit::dsl::TokenKind::kIdent,
                                     "identifier");
  if (parser.MatchSymbol(":")) {
    const auto type = parser.ExpectToken(lark::toolkit::dsl::TokenKind::kIdent,
                                         "identifier");
    return {id.text, type.text};
  }
  return {id.text, id.text};  // bare name -> id == type
}
}  // namespace

std::shared_ptr<OpGraph> OpGraphBuilder::BuildDsl(const std::string& dsl) const {
  std::vector<OpDef> defs;
  std::unordered_map<std::string, std::size_t> index;  // id -> def slot

  auto ensure = [&](const std::string& id, const std::string& type) {
    auto it = index.find(id);
    if (it != index.end()) {
      if (defs[it->second].type != type) {
        throw std::invalid_argument("OpGraphBuilder DSL: node id '" + id +
                                    "' used with two different types");
      }
      return it->second;
    }
    defs.push_back(OpDef{type, {}, id});
    index[id] = defs.size() - 1;
    return defs.size() - 1;
  };

  lark::toolkit::dsl::Parser parser(dsl, ":", {"->"});
  while (!parser.AtEnd()) {
    auto [id, type] = Segment(parser);
    ensure(id, type);
    std::optional<std::string> prev = id;
    while (parser.MatchSymbol("->")) {
      auto [nid, ntype] = Segment(parser);
      const std::size_t slot = ensure(nid, ntype);
      defs[slot].deps.push_back(*prev);
      prev = nid;
    }
    parser.MatchSymbol(";");  // optional statement separator
  }
  return Build(defs);
}

}  // namespace lark::dag
