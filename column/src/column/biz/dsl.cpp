// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/biz/dsl.h"

#include <cmath>
#include <string>

#include "toolkit/dsl.h"

namespace lark::column::biz::dsl {

namespace {

std::string Format(double v) {
  if (std::floor(v) == v && std::isfinite(v)) {
    return std::to_string(static_cast<long long>(v));
  }
  return std::to_string(v);
}

// Expression parser built on the shared toolkit::dsl framework.
class ExprParser : public lark::toolkit::dsl::Parser {
 public:
  explicit ExprParser(const std::string& src)
      : Parser(src, "+-*/=(),<>!", {">=", "<=", "==", "!="}) {}

  std::vector<exec::OpSpec> Parse() {
    while (!AtEnd()) {
      ParseStatement();
    }
    return ops_;
  }

 private:
  using Token = lark::toolkit::dsl::Token;
  using TokenKind = lark::toolkit::dsl::TokenKind;

  struct Value {
    bool is_literal = false;
    double literal = 0.0;
    std::string name;  // producing tensor name when !is_literal
  };

  std::string Temp() { return "@t" + std::to_string(tmp_++); }

  void ParseStatement() {
    const Token lhs = Consume();
    if (lhs.kind != TokenKind::kIdent) {
      Fail("dsl: statement must start with an identifier");
    }
    ExpectSymbol("=", "'='");
    Value v = ParseExpr(0);
    Assign(lhs.text, v);
  }

  void Assign(const std::string& lhs, const Value& v) {
    if (v.is_literal) {
      ops_.push_back(exec::OpSpec{"const", {}, {lhs},
                                  {{"value", Format(v.literal)}, {"size", "1"}}});
      return;
    }
    if (v.name == lhs) return;  // identity
    ops_.push_back(exec::OpSpec{"identity", {v.name}, {lhs}});
  }

  int Precedence(const std::string& op) {
    if (op == "*" || op == "/") return 10;
    if (op == "+" || op == "-") return 5;
    if (op == ">" || op == ">=" || op == "<" || op == "<=" || op == "==" ||
        op == "!=")
      return 2;
    return -1;
  }

  std::string ArithOp(const std::string& op) {
    if (op == "+") return "add";
    if (op == "-") return "sub";
    if (op == "*") return "mul";
    return "div";
  }

  std::string CmpOp(const std::string& op) {
    if (op == ">") return "gt";
    if (op == ">=") return "ge";
    if (op == "<") return "lt";
    if (op == "<=") return "le";
    if (op == "==") return "eq";
    return "neq";
  }

  Value ParseExpr(int min_prec) {
    Value left = ParsePrimary();
    for (;;) {
      const Token& t = Peek();
      if (t.kind != TokenKind::kSymbol) break;
      const int prec = Precedence(t.text);
      if (prec < min_prec) break;
      Consume();
      const std::string op = t.text;
      Value right = ParseExpr(prec + 1);
      left = MakeBinary(left, right, op);
    }
    return left;
  }

  Value MakeBinary(Value a, Value b, const std::string& op) {
    // constant fold
    if (a.is_literal && b.is_literal) {
      const double x = a.literal, y = b.literal;
      if (op == "+") return Lit(x + y);
      if (op == "-") return Lit(x - y);
      if (op == "*") return Lit(x * y);
      if (op == "/") return Lit(y == 0.0 ? 0.0 : x / y);
      if (op == ">") return Lit(x > y ? 1.0 : 0.0);
      if (op == ">=") return Lit(x >= y ? 1.0 : 0.0);
      if (op == "<") return Lit(x < y ? 1.0 : 0.0);
      if (op == "<=") return Lit(x <= y ? 1.0 : 0.0);
      if (op == "==") return Lit(x == y ? 1.0 : 0.0);
      if (op == "!=") return Lit(x != y ? 1.0 : 0.0);
    }

    const std::string out = Temp();
    const bool cmp =
        (op == ">" || op == ">=" || op == "<" || op == "<=" || op == "==" ||
         op == "!=");
    const std::string base = cmp ? CmpOp(op) : ArithOp(op);
    if (cmp) {
      // comparison with a scalar literal folds into a *_scalar op
      if (a.is_literal) {
        ops_.push_back(exec::OpSpec{base + "_scalar", {b.name}, {out},
                                    {{"scalar", Format(a.literal)}}});
        return Named(out);
      }
      if (b.is_literal) {
        ops_.push_back(exec::OpSpec{base + "_scalar", {a.name}, {out},
                                    {{"scalar", Format(b.literal)}}});
        return Named(out);
      }
    } else {
      if (a.is_literal) {
        ops_.push_back(exec::OpSpec{base + "_scalar", {b.name}, {out},
                                    {{"scalar", Format(a.literal)}}});
        return Named(out);
      }
      if (b.is_literal) {
        ops_.push_back(exec::OpSpec{base + "_scalar", {a.name}, {out},
                                    {{"scalar", Format(b.literal)}}});
        return Named(out);
      }
    }
    ops_.push_back(exec::OpSpec{base, {a.name, b.name}, {out}});
    return Named(out);
  }

  Value ParsePrimary() {
    const Token t = Consume();
    if (t.kind == TokenKind::kNumber) return Lit(t.number);
    if (t.kind == TokenKind::kIdent) {
      if (Peek().kind == TokenKind::kSymbol && Peek().text == "(") {
        Consume();
        return ParseCall(t.text);
      }
      return Named(t.text);
    }
    if (t.kind == TokenKind::kSymbol && t.text == "-") {
      Value v = ParsePrimary();
      if (v.is_literal) return Lit(-v.literal);
      const std::string out = Temp();
      ops_.push_back(exec::OpSpec{"neg", {v.name}, {out}});
      return Named(out);
    }
    if (t.kind == TokenKind::kSymbol && t.text == "(") {
      Value v = ParseExpr(0);
      ExpectSymbol(")", "')'");
      return v;
    }
    Fail("dsl: unexpected token");
  }

  Value ParseCall(const std::string& fn) {
    const std::string out = Temp();

    // Turn a literal argument into a length-1 const column so ops can read it.
    auto materialize = [&](const Value& v) -> std::string {
      if (!v.is_literal) return v.name;
      const std::string cname = Temp();
      ops_.push_back(exec::OpSpec{"const", {}, {cname},
                                  {{"value", Format(v.literal)}, {"size", "1"}}});
      return cname;
    };

    if (fn == "select") {
      Value mask = ParseExpr(0);
      ExpectSymbol(",", "','");
      Value a = ParseExpr(0);
      ExpectSymbol(",", "','");
      Value b = ParseExpr(0);
      ExpectSymbol(")", "')'");
      ops_.push_back(exec::OpSpec{"select",
                                  {materialize(mask), materialize(a),
                                   materialize(b)},
                                  {out}});
      return Named(out);
    }
    if (fn == "filter") {
      Value data = ParseExpr(0);
      ExpectSymbol(",", "','");
      Value mask = ParseExpr(0);
      ExpectSymbol(")", "')'");
      ops_.push_back(
          exec::OpSpec{"filter", {materialize(data), materialize(mask)}, {out}});
      return Named(out);
    }
    if (fn == "dot") {
      Value a = ParseExpr(0);
      ExpectSymbol(",", "','");
      Value b = ParseExpr(0);
      ExpectSymbol(")", "')'");
      ops_.push_back(exec::OpSpec{"dot", {materialize(a), materialize(b)}, {out}});
      return Named(out);
    }
    if (fn == "cast") {
      Value a = ParseExpr(0);
      ExpectSymbol(",", "','");
      const Token dtype = Consume();
      if (dtype.kind != TokenKind::kIdent) {
        Fail("dsl: cast expects a dtype name");
      }
      ExpectSymbol(")", "')'");
      ops_.push_back(exec::OpSpec{"cast", {materialize(a)}, {out},
                                  {{"dtype", dtype.text}}});
      return Named(out);
    }

    // single-argument functions
    static const char* kUnary[] = {"sum",   "mean",   "count", "max",
                                   "min",   "stddev", "abs",   "neg",
                                   "square", "sqrt",  "exp",   "log",
                                   "quantize", "dequantize"};
    bool known = false;
    for (const char* u : kUnary) {
      if (fn == u) {
        known = true;
        break;
      }
    }
    if (!known) Fail("dsl: unknown function '" + fn + "'");

    Value a = ParseExpr(0);
    ExpectSymbol(")", "')'");
    ops_.push_back(exec::OpSpec{fn, {materialize(a)}, {out}});
    return Named(out);
  }

  Value Lit(double v) { return Value{true, v, ""}; }
  Value Named(std::string name) { return Value{false, 0.0, std::move(name)}; }

  std::vector<exec::OpSpec> ops_;
  std::size_t tmp_ = 0;
};

}  // namespace

std::vector<exec::OpSpec> parse(const std::string& source) {
  ExprParser parser(source);
  return parser.Parse();
}

}  // namespace lark::column::biz::dsl
