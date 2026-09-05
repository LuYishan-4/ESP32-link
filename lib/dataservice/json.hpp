// lib/dataservice/json.hpp
// Minimal, dependency-free JSON parser + value tree.
// Supports the subset DataService needs: objects, arrays, strings, numbers,
// booleans and null (with standard escapes incl. \uXXXX).
#pragma once

#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <cstdlib>
#include <cctype>

namespace js {

// A parsed JSON value. Only the members relevant to the current type are used.
class Value {
public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool b = false;                                   // Bool
  double num = 0.0;                                 // Number
  std::string str;                                  // String
  std::vector<std::unique_ptr<Value>> items;        // Array children
  std::vector<std::pair<std::string, std::unique_ptr<Value>>> fields; // Object children

  bool isNull()   const { return type == Type::Null; }
  bool isBool()   const { return type == Type::Bool; }
  bool isNumber() const { return type == Type::Number; }
  bool isString() const { return type == Type::String; }
  bool isArray()  const { return type == Type::Array; }
  bool isObject() const { return type == Type::Object; }

  // Object lookup (first match wins when a key is duplicated).
  Value* find(const std::string& key) {
    for (auto& f : fields) if (f.first == key) return f.second.get();
    return nullptr;
  }
  const Value* find(const std::string& key) const {
    for (const auto& f : fields) if (f.first == key) return f.second.get();
    return nullptr;
  }

  size_t size() const { return items.size(); }
  Value* at(size_t i) { return i < items.size() ? items[i].get() : nullptr; }
  const Value* at(size_t i) const { return i < items.size() ? items[i].get() : nullptr; }
};

// Render a scalar the way the old loader did: strings unchanged, numbers as
// integer text, booleans as "true"/"false", everything else empty.
inline std::string scalarString(const Value& v) {
  switch (v.type) {
    case Value::Type::String: return v.str;
    case Value::Type::Bool:   return v.b ? "true" : "false";
    case Value::Type::Number: return std::to_string(static_cast<long long>(v.num));
    default:                  return std::string();
  }
}

namespace detail {

class Parser {
public:
  explicit Parser(const std::string& text) : s_(text) {}

  // Parses one JSON value; the whole input must be consumed on success.
  std::unique_ptr<Value> parse() {
    std::unique_ptr<Value> root = parseValue();
    if (!root) return nullptr;
    skipWs();
    return pos_ == s_.size() ? std::move(root) : nullptr;
  }

private:
  const std::string& s_;
  size_t pos_ = 0;

  void skipWs() {
    while (pos_ < s_.size() &&
           std::isspace(static_cast<unsigned char>(s_[pos_])))
      ++pos_;
  }
  bool atEnd() const { return pos_ >= s_.size(); }
  char peek() const { return atEnd() ? '\0' : s_[pos_]; }
  char take() { return atEnd() ? '\0' : s_[pos_++]; }

  bool expect(char c) {
    if (!atEnd() && s_[pos_] == c) { ++pos_; return true; }
    return false;
  }

  static std::unique_ptr<Value> makeNode(Value::Type t) {
    auto n = std::make_unique<Value>();
    n->type = t;
    return n;
  }

  std::unique_ptr<Value> parseValue() {
    skipWs();
    const char c = peek();
    switch (c) {
      case '{': return parseObject();
      case '[': return parseArray();
      case '"': return parseString();
      case 't': return parseLiteral("true", Value::Type::Bool, true);
      case 'f': return parseLiteral("false", Value::Type::Bool, false);
      case 'n': return parseLiteral("null", Value::Type::Null, false);
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        return nullptr;
    }
  }

  bool readHex4(unsigned& out) {
    out = 0;
    for (int k = 0; k < 4; ++k) {
      if (atEnd()) return false;
      const char h = take();
      out <<= 4;
      if (h >= '0' && h <= '9')      out |= static_cast<unsigned>(h - '0');
      else if (h >= 'a' && h <= 'f') out |= static_cast<unsigned>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') out |= static_cast<unsigned>(h - 'A' + 10);
      else return false;
    }
    return true;
  }

  static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  std::unique_ptr<Value> parseString() {
    if (!expect('"')) return nullptr;
    auto n = makeNode(Value::Type::String);
    std::string out;
    while (!atEnd()) {
      const char c = take();
      if (c == '"') { n->str = std::move(out); return n; }
      if (c != '\\') { out.push_back(c); continue; }
      if (atEnd()) return nullptr;
      const char e = take();
      switch (e) {
        case '"':  out.push_back('"');  break;
        case '\\': out.push_back('\\'); break;
        case '/':  out.push_back('/');  break;
        case 'b':  out.push_back('\b'); break;
        case 'f':  out.push_back('\f'); break;
        case 'n':  out.push_back('\n'); break;
        case 'r':  out.push_back('\r'); break;
        case 't':  out.push_back('\t'); break;
        case 'u': {
          unsigned cp = 0;
          if (!readHex4(cp)) return nullptr;
          // Combine a UTF-16 surrogate pair when present.
          if (cp >= 0xD800 && cp <= 0xDBFF && !atEnd() && peek() == '\\') {
            size_t save = pos_;
            take(); // consume '\'
            if (!atEnd() && take() == 'u') {
              unsigned lo = 0;
              if (readHex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              } else {
                pos_ = save; // lone high surrogate: keep as-is below
              }
            } else {
              pos_ = save;
            }
          }
          appendUtf8(out, cp);
          break;
        }
        default: return nullptr;
      }
    }
    return nullptr; // unterminated string
  }

  std::unique_ptr<Value> parseLiteral(const char* lit, Value::Type t, bool bv) {
    for (const char* p = lit; *p; ++p) {
      if (atEnd() || take() != *p) return nullptr;
    }
    auto n = makeNode(t);
    n->b = bv;
    return n;
  }

  std::unique_ptr<Value> parseNumber() {
    const size_t start = pos_;
    if (peek() == '-') take();
    if (atEnd() || !(std::isdigit(static_cast<unsigned char>(peek()))))
      return nullptr;
    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) take();
    if (!atEnd() && peek() == '.') {
      take();
      if (atEnd() || !std::isdigit(static_cast<unsigned char>(peek())))
        return nullptr;
      while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) take();
    }
    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
      take();
      if (!atEnd() && (peek() == '+' || peek() == '-')) take();
      if (atEnd() || !std::isdigit(static_cast<unsigned char>(peek())))
        return nullptr;
      while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) take();
    }
    const std::string token = s_.substr(start, pos_ - start);
    char* endp = nullptr;
    const double d = std::strtod(token.c_str(), &endp);
    if (!endp || *endp != '\0') return nullptr;
    auto n = makeNode(Value::Type::Number);
    n->num = d;
    return n;
  }

  std::unique_ptr<Value> parseArray() {
    if (!expect('[')) return nullptr;
    auto n = makeNode(Value::Type::Array);
    skipWs();
    if (expect(']')) return n;
    while (true) {
      skipWs();
      std::unique_ptr<Value> v = parseValue();
      if (!v) return nullptr;
      n->items.push_back(std::move(v));
      skipWs();
      if (expect(']')) return n;
      if (!expect(',')) return nullptr;
    }
  }

  std::unique_ptr<Value> parseObject() {
    if (!expect('{')) return nullptr;
    auto n = makeNode(Value::Type::Object);
    skipWs();
    if (expect('}')) return n;
    while (true) {
      skipWs();
      std::unique_ptr<Value> key = parseString();
      if (!key) return nullptr;
      skipWs();
      if (!expect(':')) return nullptr;
      skipWs();
      std::unique_ptr<Value> v = parseValue();
      if (!v) return nullptr;
      n->fields.emplace_back(key->str, std::move(v));
      skipWs();
      if (expect('}')) return n;
      if (!expect(',')) return nullptr;
    }
  }
};

} // namespace detail

// Parses `text`; returns the root value, or nullptr when the input is not a
// single valid JSON document. `err`, when given, receives a short reason.
inline std::unique_ptr<Value> parse(const std::string& text, std::string* err = nullptr) {
  detail::Parser parser(text);
  std::unique_ptr<Value> root = parser.parse();
  if (!root && err) *err = "invalid JSON";
  return root;
}

} // namespace js
