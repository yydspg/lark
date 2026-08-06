// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "toolkit/dsl.h"

#include <algorithm>
#include <cctype>

namespace lark::toolkit::dsl {

// ─────────────────────────────────────────────────────────────────────────────
// Lexer
// ─────────────────────────────────────────────────────────────────────────────
Lexer::Lexer(std::string_view source, std::string_view symbols,
             std::vector<std::string> multi)
    : source_(std::string(source)), symbols_(symbols), multi_(std::move(multi)) {
  // longest first, so "->" wins over "-" when both are present
  std::sort(multi_.begin(), multi_.end(),
            [](const std::string& a, const std::string& b) {
              return a.size() > b.size();
            });
}

void Lexer::SkipSpace() {
  while (pos_ < source_.size()) {
    const char c = source_[pos_];
    if (c == '#') {  // comment to end of line
      while (pos_ < source_.size() && source_[pos_] != '\n') {
        ++pos_;
        ++col_;
      }
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (c == '\n') {
        ++line_;
        col_ = 1;
      } else {
        ++col_;
      }
      ++pos_;
      continue;
    }
    break;
  }
}

Token Lexer::Lex() {
  SkipSpace();
  const std::size_t offset = pos_;
  const std::size_t line = line_;
  const std::size_t col = col_;

  if (pos_ >= source_.size()) {
    return Token{TokenKind::kEnd, "", 0.0, offset, line, col};
  }
  const char c = source_[pos_];

  // number: [0-9]+ or '.'[0-9]
  if (std::isdigit(static_cast<unsigned char>(c)) ||
      (c == '.' && pos_ + 1 < source_.size() &&
       std::isdigit(static_cast<unsigned char>(source_[pos_ + 1])))) {
    const std::size_t start = pos_;
    while (pos_ < source_.size() &&
           (std::isdigit(static_cast<unsigned char>(source_[pos_])) ||
            source_[pos_] == '.')) {
      ++pos_;
      ++col_;
    }
    Token t;
    t.kind = TokenKind::kNumber;
    t.text = std::string(source_.substr(start, pos_ - start));
    t.number = std::stod(t.text);
    t.offset = offset;
    t.line = line;
    t.col = col;
    return t;
  }

  // identifier
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
    const std::size_t start = pos_;
    while (pos_ < source_.size() &&
           (std::isalnum(static_cast<unsigned char>(source_[pos_])) ||
            source_[pos_] == '_')) {
      ++pos_;
      ++col_;
    }
    Token t;
    t.kind = TokenKind::kIdent;
    t.text = std::string(source_.substr(start, pos_ - start));
    t.offset = offset;
    t.line = line;
    t.col = col;
    return t;
  }

  // multi-char symbols (longest first)
  for (const auto& sym : multi_) {
    if (source_.substr(pos_, sym.size()) == sym) {
      pos_ += sym.size();
      col_ += sym.size();
      return Token{TokenKind::kSymbol, sym, 0.0, offset, line, col};
    }
  }

  // single-char symbols
  if (symbols_.find(c) != std::string_view::npos) {
    ++pos_;
    ++col_;
    return Token{TokenKind::kSymbol, std::string(1, c), 0.0, offset, line,
                 col};
  }

  throw DslError(std::string("unexpected character '") + c + "'", line, col);
}

const Token& Lexer::Peek() {
  if (!peeked_) {
    peek_ = Lex();
    peeked_ = true;
  }
  return peek_;
}

Token Lexer::Next() {
  const Token t = Peek();
  peeked_ = false;
  return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser
// ─────────────────────────────────────────────────────────────────────────────
void Parser::Fail(const std::string& message) {
  std::size_t line = 0;
  std::size_t col = 0;
  try {
    const Token& t = lexer_.Peek();  // may be cached; best-effort position
    line = t.line;
    col = t.col;
  } catch (...) {
    line = 0;
    col = 0;
  }
  throw DslError(message, line, col);
}

}  // namespace lark::toolkit::dsl
