// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// A small, reusable DSL parsing framework.
//
// Column's expression DSL (feed/compute/fetch rules) and dag's arrow DSL
// (Op graph description) share this machinery: a configurable tokenizer plus a
// recursive-descent Parser base with position-aware errors. New DSLs reuse the
// same Lexer/Parser instead of re-implementing token handling.

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lark::toolkit::dsl {

// ─────────────────────────────────────────────────────────────────────────────
// Tokens
// ─────────────────────────────────────────────────────────────────────────────
enum class TokenKind { kIdent, kNumber, kSymbol, kEnd };

struct Token {
  TokenKind kind = TokenKind::kEnd;
  std::string text;      // identifier text / symbol text
  double number = 0.0;   // value when kind == kNumber
  std::size_t offset = 0;
  std::size_t line = 1;
  std::size_t col = 1;
};

// Parse error carrying the source position.
class DslError : public std::runtime_error {
 public:
  DslError(std::string message, std::size_t line, std::size_t col)
      : std::runtime_error(std::move(message)),
        line_(line),
        col_(col) {}

  std::size_t Line() const noexcept { return line_; }
  std::size_t Col() const noexcept { return col_; }

 private:
  std::size_t line_;
  std::size_t col_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Lexer: tokenizes identifiers, numbers, and a configurable symbol set.
//
//   symbols — 1-char symbols (e.g. "+-*/=(),<>!")
//   multi   — multi-char symbols, longest match wins (e.g. {">=", "==", "->"})
//
// Whitespace and '#'-to-end-of-line comments are skipped. Identifiers are
// [A-Za-z_][A-Za-z0-9_]*; numbers are [0-9]+('.'[0-9]+)? (leading '.' allowed).
// ─────────────────────────────────────────────────────────────────────────────
class Lexer {
 public:
  Lexer(std::string_view source, std::string_view symbols,
        std::vector<std::string> multi = {});

  const Token& Peek();
  Token Next();
  bool AtEnd() { return Peek().kind == TokenKind::kEnd; }

  std::string_view Source() const noexcept { return source_; }

 private:
  void SkipSpace();
  Token Lex();

  std::string source_;  // owns a copy so the lexer can outlive the caller string
  std::string_view symbols_;
  std::vector<std::string> multi_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
  std::size_t col_ = 1;
  Token peek_{};
  bool peeked_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Parser: recursive-descent base over a token stream.
//
// Subclasses implement their grammar using Peek / Consume / Match / Expect and
// fail with position-aware DslError:
//
//   class MyParser : public dsl::Parser {
//    public:
//     MyParser(const std::string& s) : Parser(s, "+-*/=()", {">=", "->"}) {}
//     // ... grammar methods ...
//   };
// ─────────────────────────────────────────────────────────────────────────────
class Parser {
 public:
  Parser(std::string_view source, std::string_view symbols,
         std::vector<std::string> multi = {})
      : lexer_(source, symbols, std::move(multi)) {}

  const Token& Peek() { return lexer_.Peek(); }
  Token Consume() {
    Token t = Peek();
    lexer_.Next();
    return t;
  }

  bool Match(TokenKind kind) {
    if (Peek().kind == kind) {
      lexer_.Next();
      return true;
    }
    return false;
  }
  bool MatchSymbol(const std::string& symbol) {
    if (Peek().kind == TokenKind::kSymbol && Peek().text == symbol) {
      lexer_.Next();
      return true;
    }
    return false;
  }

  // Consume and validate the next token of the given kind.
  void Expect(TokenKind kind, const char* what) {
    if (Peek().kind != kind) Fail(std::string("expected ") + what);
    lexer_.Next();
  }
  // Consume and validate the next token of the given kind; returns it.
  Token ExpectToken(TokenKind kind, const char* what) {
    const Token t = Peek();
    if (t.kind != kind) Fail(std::string("expected ") + what);
    lexer_.Next();
    return t;
  }
  // Consume and validate the next symbol.
  void ExpectSymbol(const std::string& symbol, const char* what) {
    if (Peek().kind != TokenKind::kSymbol || Peek().text != symbol) {
      Fail(std::string("expected ") + what);
    }
    lexer_.Next();
  }

  bool AtEnd() { return lexer_.AtEnd(); }

  [[noreturn]] void Fail(const std::string& message);

 protected:
  Lexer lexer_;
};

}  // namespace lark::toolkit::dsl
