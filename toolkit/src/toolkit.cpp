// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "toolkit/str.h"
#include "toolkit/time.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <sstream>

namespace lark::toolkit::str {

std::vector<std::string> Split(std::string_view s, std::string_view delim,
                               bool skip_empty) {
  std::vector<std::string> out;
  if (delim.empty()) {
    out.emplace_back(s);
    return out;
  }
  std::size_t start = 0;
  while (true) {
    const std::size_t pos = s.find(delim, start);
    const std::string_view part =
        pos == std::string_view::npos ? s.substr(start) : s.substr(start, pos - start);
    if (!(skip_empty && part.empty())) out.emplace_back(part);
    if (pos == std::string_view::npos) break;
    start = pos + delim.size();
  }
  return out;
}

std::vector<std::string> SplitAny(std::string_view s, std::string_view delims) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || delims.find(s[i]) != std::string_view::npos) {
      out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

std::string Join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string Join(const std::vector<std::string_view>& parts,
                 std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.remove_suffix(1);
  return s;
}

std::string_view Trim(std::string_view s, std::string_view chars) {
  while (!s.empty() && chars.find(s.front()) != std::string_view::npos)
    s.remove_prefix(1);
  while (!s.empty() && chars.find(s.back()) != std::string_view::npos)
    s.remove_suffix(1);
  return s;
}

bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.substr(s.size() - suffix.size()) == suffix;
}

bool Contains(std::string_view s, std::string_view needle) {
  return s.find(needle) != std::string_view::npos;
}

std::string ToLower(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string ToUpper(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

std::string ReplaceAll(std::string s, std::string_view from,
                       std::string_view to) {
  if (from.empty()) return s;
  std::size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

bool ToInt64(std::string_view s, int64_t& out) {
  if (s.empty()) return false;
  errno = 0;
  char* end = nullptr;
  const std::string tmp(s);
  const long long v = std::strtoll(tmp.c_str(), &end, 10);
  if (errno != 0 || end != tmp.c_str() + tmp.size()) return false;
  out = static_cast<int64_t>(v);
  return true;
}

bool ToDouble(std::string_view s, double& out) {
  if (s.empty()) return false;
  errno = 0;
  char* end = nullptr;
  const std::string tmp(s);
  const double v = std::strtod(tmp.c_str(), &end);
  if (errno != 0 || end != tmp.c_str() + tmp.size()) return false;
  out = v;
  return true;
}

bool IsBlank(std::string_view s) { return Trim(s).empty(); }

}  // namespace lark::toolkit::str

namespace lark::toolkit::time {

std::string FormatDuration(std::chrono::nanoseconds d) {
  const auto ns = d.count();
  std::ostringstream os;
  os << std::fixed;
  os.precision(1);
  if (ns < 1000) {
    os << ns << "ns";
  } else if (ns < 1000 * 1000) {
    os << (static_cast<double>(ns) / 1000.0) << "us";
  } else if (ns < 1000LL * 1000 * 1000) {
    os << (static_cast<double>(ns) / (1000.0 * 1000.0)) << "ms";
  } else {
    os << (static_cast<double>(ns) / (1000.0 * 1000.0 * 1000.0)) << "s";
  }
  return os.str();
}

}  // namespace lark::toolkit::time
