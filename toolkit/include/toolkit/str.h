// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Generic string utilities — the home for the small helpers that would
// otherwise get copy-pasted across modules.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lark::toolkit::str {

// Split `s` on every occurrence of `delim`.
std::vector<std::string> Split(std::string_view s, std::string_view delim,
                               bool skip_empty = false);

// Split on any single character in `delims`.
std::vector<std::string> SplitAny(std::string_view s, std::string_view delims);

// Join parts with `sep` between them.
std::string Join(const std::vector<std::string>& parts, std::string_view sep);
std::string Join(const std::vector<std::string_view>& parts,
                 std::string_view sep);

// Trim ASCII whitespace (or the given character set) from both ends.
std::string_view Trim(std::string_view s);
std::string_view Trim(std::string_view s, std::string_view chars);

bool StartsWith(std::string_view s, std::string_view prefix);
bool EndsWith(std::string_view s, std::string_view suffix);
bool Contains(std::string_view s, std::string_view needle);

// Case conversion (ASCII).
std::string ToLower(std::string_view s);
std::string ToUpper(std::string_view s);

// Replace every occurrence of `from` with `to`.
std::string ReplaceAll(std::string s, std::string_view from,
                       std::string_view to);

// Numeric parsing with strict validation.
bool ToInt64(std::string_view s, int64_t& out);
bool ToDouble(std::string_view s, double& out);

// True when the string is empty or only whitespace.
bool IsBlank(std::string_view s);

}  // namespace lark::toolkit::str
