// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/biz/dsl.h"

#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>

namespace lark::column::biz::dsl {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Tokenizer
// ─────────────────────────────────────────────────────────────────────────────
enum class TokKind { kNumber, kIdent, kOp, kAssign, kLParen, kRParen, kComma, kEnd };

struct Token {
  TokKind kind;
  std::string text;
  double number = 0.0;
};

class Lexer {
 public:
  explicit Lexer(const std::string& src) : src_(src) {}

  Token Next() {
    SkipSpaces();
    if (pos_ >= src_.size()) return Token{TokKind::kEnd, ""};
    const char c = src_[pos_];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
      const size_t start = pos_;
      while (pos_ < src_.size() &&
             (std::isdigit(static_cast<unsigned char>(src_[pos_])) ||
              src_[pos_] == '.'))
        ++pos_;
      const std::string text = src_.substr(start, pos_ - start);
      return Token{TokKind::kNumber, text, std::stod(text)};
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      const size_t start = pos_;
      while (pos_ < src_.size() &&
             (std::isalnum(static_cast<unsigned char>(src_[pos_])) ||
              src_[pos_] == '_'))
        ++pos_;
      return Token{TokKind::kIdent, src_.substr(start, pos_ - start)};
    }
    switch (c) {
      case '(':
        ++pos_;
        return Token{TokKind::kLParen, "("};
      case ')':
        ++pos_;
        return Token{TokKind::kRParen, ")"};
      case ',':
        ++pos_;
        return Token{TokKind::kComma, ","};
      case '=':
        ++pos_;
        return Token{TokKind::kAssign, "="};
      case '>':
      case '<':
      case '!':
      case '+':
      case '-':
      case '*':
      case '/': {
        std::string text(1, c);
        ++pos_;
        if ((c == '>' || c == '<' || c == '!') && pos_ < src_.size() &&
            src_[pos_] == '=') {
          text += '=';
          ++pos_;
        }
        return Token{TokKind::kOp, text};
      }
      default:
        throw std::invalid_argument("dsl: unexpected character '" +
                                    std::string(1, c) + "'");
    }
  }

  bool Match(TokKind kind) {
    if (peek_.kind == kind) {
      Consume();
      return true;
    }
    return false;
  }

  const Token& Peek() {
    if (!peeked_) {
      peek_ = Next();
      peeked_ = true;
    }
    return peek_;
  }

  Token Consume() {
    Token t = Peek();
    peeked_ = false;
    return t;
  }

  void Expect(TokKind kind, const char* what) {
    const Token& t = Peek();
    if (t.kind != kind)
      throw std::invalid_argument(std::string("dsl: expected ") + what);
    Consume();
  }

 private:
  void SkipSpaces() {
    while (pos_ < src_.size() &&
           std::isspace(static_cast<unsigned char>(src_[pos_])))
      ++pos_;
  }

  const std::string& src_;
  size_t pos_ = 0;
  Token peek_{TokKind::kEnd, ""};
  bool peeked_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Recursive-descent parser → business ops
// ─────────────────────────────────────────────────────────────────────────────
std::string Format(double v) {
  if (std::floor(v) == v && std::isfinite(v)) {
    return std::to_string(static_cast<long long>(v));
  }
  return std::to_string(v);
}

class Parser {
 public:
  explicit Parser(const std::string& src) : lex_(src) {}

  std::vector<exec::OpSpec> Parse() {
    while (lex_.Peek().kind != TokKind::kEnd) {
      ParseStatement();
    }
    return ops_;
  }

 private:
  struct Value {
    bool is_literal = false;
    double literal = 0.0;
    std::string name;  // producing tensor name when !is_literal
  };

  std::string Temp() { return "@t" + std::to_string(tmp_++); }

  void ParseStatement() {
    const Token lhs = lex_.Consume();
    if (lhs.kind != TokKind::kIdent)
      throw std::invalid_argument("dsl: statement must start with an identifier");
    lex_.Expect(TokKind::kAssign, "'='");
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
      const Token& t = lex_.Peek();
      if (t.kind != TokKind::kOp) break;
      const int prec = Precedence(t.text);
      if (prec < min_prec) break;
      lex_.Consume();
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
    const Token t = lex_.Consume();
    if (t.kind == TokKind::kNumber) return Lit(t.number);
    if (t.kind == TokKind::kIdent) {
      const Token& next = lex_.Peek();
      if (next.kind == TokKind::kLParen) {
        lex_.Consume();
        return ParseCall(t.text);
      }
      return Named(t.text);
    }
    if (t.kind == TokKind::kOp && t.text == "-") {
      Value v = ParsePrimary();
      if (v.is_literal) return Lit(-v.literal);
      const std::string out = Temp();
      ops_.push_back(exec::OpSpec{"neg", {v.name}, {out}});
      return Named(out);
    }
    if (t.kind == TokKind::kLParen) {
      Value v = ParseExpr(0);
      lex_.Expect(TokKind::kRParen, "')'");
      return v;
    }
    throw std::invalid_argument("dsl: unexpected token");
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
      lex_.Expect(TokKind::kComma, "','");
      Value a = ParseExpr(0);
      lex_.Expect(TokKind::kComma, "','");
      Value b = ParseExpr(0);
      lex_.Expect(TokKind::kRParen, "')'");
      ops_.push_back(exec::OpSpec{"select",
                                  {materialize(mask), materialize(a),
                                   materialize(b)},
                                  {out}});
      return Named(out);
    }
    if (fn == "filter") {
      Value data = ParseExpr(0);
      lex_.Expect(TokKind::kComma, "','");
      Value mask = ParseExpr(0);
      lex_.Expect(TokKind::kRParen, "')'");
      ops_.push_back(
          exec::OpSpec{"filter", {materialize(data), materialize(mask)}, {out}});
      return Named(out);
    }
    if (fn == "dot") {
      Value a = ParseExpr(0);
      lex_.Expect(TokKind::kComma, "','");
      Value b = ParseExpr(0);
      lex_.Expect(TokKind::kRParen, "')'");
      ops_.push_back(exec::OpSpec{"dot", {materialize(a), materialize(b)}, {out}});
      return Named(out);
    }
    if (fn == "cast") {
      Value a = ParseExpr(0);
      lex_.Expect(TokKind::kComma, "','");
      const Token dtype = lex_.Consume();
      if (dtype.kind != TokKind::kIdent)
        throw std::invalid_argument("dsl: cast expects a dtype name");
      lex_.Expect(TokKind::kRParen, "')'");
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
    if (!known)
      throw std::invalid_argument("dsl: unknown function '" + fn + "'");

    Value a = ParseExpr(0);
    lex_.Expect(TokKind::kRParen, "')'");
    ops_.push_back(exec::OpSpec{fn, {materialize(a)}, {out}});
    return Named(out);
  }

  Value Lit(double v) { return Value{true, v, ""}; }
  Value Named(std::string name) { return Value{false, 0.0, std::move(name)}; }

  Lexer lex_;
  std::vector<exec::OpSpec> ops_;
  size_t tmp_ = 0;
};

}  // namespace

std::vector<exec::OpSpec> parse(const std::string& source) {
  Parser parser(source);
  return parser.Parse();
}

}  // namespace lark::column::biz::dsl
