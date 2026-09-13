#include "weasel_ai_json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace weasel_ai {

namespace {

const JsonPtr kNullJson;

void AppendUtf8(std::string& out, unsigned int cp) {
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

class Parser {
 public:
  Parser(const std::string& text) : text_(text) {}

  JsonPtr Run(std::string* error) {
    SkipWs();
    auto root = ParseValue();
    if (!root) {
      if (error && error->empty())
        *error = "invalid JSON";
      return kNullJson;
    }
    SkipWs();
    if (pos_ != text_.size()) {
      if (error)
        *error = "trailing characters after JSON value";
      return kNullJson;
    }
    if (error)
      error->clear();
    return root;
  }

 private:
  const std::string& text_;
  size_t pos_ = 0;
  int depth_ = 0;
  static constexpr int kMaxDepth = 32;
  static constexpr size_t kMaxStringSize = 1 << 20;

  bool AtEnd() const { return pos_ >= text_.size(); }
  char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }
  char Next() { return AtEnd() ? '\0' : text_[pos_++]; }
  void SkipWs() {
    while (!AtEnd()) {
      char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        ++pos_;
      else
        break;
    }
  }
  bool Consume(char c) {
    if (Peek() == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool Expect(const char* lit) {
    for (const char* p = lit; *p; ++p) {
      if (Next() != *p)
        return false;
    }
    return true;
  }

  JsonPtr ParseValue() {
    if (++depth_ > kMaxDepth)
      return kNullJson;
    JsonPtr result;
    switch (Peek()) {
      case '{':
        result = ParseObject();
        break;
      case '[':
        result = ParseArray();
        break;
      case '"':
        result = ParseStringOnly();
        break;
      case 't':
        if (Expect("true"))
          result = JsonValue::MakeBool(true);
        break;
      case 'f':
        if (Expect("false"))
          result = JsonValue::MakeBool(false);
        break;
      case 'n':
        if (Expect("null"))
          result = std::make_shared<JsonValue>();
        break;
      default:
        result = ParseNumber();
        break;
    }
    --depth_;
    return result;
  }

  JsonPtr ParseObject() {
    if (!Consume('{'))
      return kNullJson;
    auto obj = JsonValue::MakeObject();
    SkipWs();
    if (Consume('}'))
      return obj;
    while (true) {
      SkipWs();
      if (Peek() != '"')
        return kNullJson;
      JsonPtr key = ParseStringOnly();
      if (!key)
        return kNullJson;
      SkipWs();
      if (!Consume(':'))
        return kNullJson;
      SkipWs();
      JsonPtr value = ParseValue();
      if (!value)
        return kNullJson;
      obj->set(key->as_string(), value);
      SkipWs();
      if (Consume(','))
        continue;
      if (Consume('}'))
        return obj;
      return kNullJson;
    }
  }

  JsonPtr ParseArray() {
    if (!Consume('['))
      return kNullJson;
    auto arr = JsonValue::MakeArray();
    SkipWs();
    if (Consume(']'))
      return arr;
    while (true) {
      SkipWs();
      JsonPtr value = ParseValue();
      if (!value)
        return kNullJson;
      arr->push(value);
      SkipWs();
      if (Consume(','))
        continue;
      if (Consume(']'))
        return arr;
      return kNullJson;
    }
  }

  // Parses a quoted string and returns a String JsonValue.
  JsonPtr ParseStringOnly() {
    std::string out;
    if (!ParseStringInto(&out))
      return kNullJson;
    return JsonValue::MakeString(std::move(out));
  }

  bool ParseHex4(unsigned int* value) {
    unsigned int v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = Next();
      v <<= 4;
      if (c >= '0' && c <= '9')
        v |= static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f')
        v |= static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        v |= static_cast<unsigned int>(c - 'A' + 10);
      else
        return false;
    }
    *value = v;
    return true;
  }

  bool ParseStringInto(std::string* out) {
    if (!Consume('"'))
      return false;
    size_t start = pos_;
    while (true) {
      if (AtEnd() || pos_ - start > kMaxStringSize)
        return false;
      char c = Next();
      if (c == '"')
        break;
      if (static_cast<unsigned char>(c) < 0x20)
        return false;  // raw control chars are not allowed in JSON strings
      if (c != '\\') {
        out->push_back(c);
        continue;
      }
      char esc = Next();
      switch (esc) {
        case '"':
          out->push_back('"');
          break;
        case '\\':
          out->push_back('\\');
          break;
        case '/':
          out->push_back('/');
          break;
        case 'b':
          out->push_back('\b');
          break;
        case 'f':
          out->push_back('\f');
          break;
        case 'n':
          out->push_back('\n');
          break;
        case 'r':
          out->push_back('\r');
          break;
        case 't':
          out->push_back('\t');
          break;
        case 'u': {
          unsigned int cp = 0;
          if (!ParseHex4(&cp))
            return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            // high surrogate: expect a low surrogate next
            if (!Consume('\\') || !Consume('u'))
              return false;
            unsigned int low = 0;
            if (!ParseHex4(&low))
              return false;
            if (low < 0xDC00 || low > 0xDFFF)
              return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return false;  // lone low surrogate
          }
          AppendUtf8(*out, cp);
          break;
        }
        default:
          return false;
      }
    }
    return true;
  }

  JsonPtr ParseNumber() {
    size_t start = pos_;
    if (Peek() == '-')
      ++pos_;
    if (Peek() < '0' || Peek() > '9')
      return kNullJson;
    while (Peek() >= '0' && Peek() <= '9')
      ++pos_;
    if (Consume('.')) {
      if (Peek() < '0' || Peek() > '9')
        return kNullJson;
      while (Peek() >= '0' && Peek() <= '9')
        ++pos_;
    }
    if (Peek() == 'e' || Peek() == 'E') {
      ++pos_;
      if (Peek() == '+' || Peek() == '-')
        ++pos_;
      if (Peek() < '0' || Peek() > '9')
        return kNullJson;
      while (Peek() >= '0' && Peek() <= '9')
        ++pos_;
    }
    try {
      return JsonValue::MakeNumber(std::stod(text_.substr(start, pos_ - start)));
    } catch (...) {
      return kNullJson;
    }
  }
};

void EscapeInto(const std::string& s, std::string* out) {
  out->push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        *out += "\\\"";
        break;
      case '\\':
        *out += "\\\\";
        break;
      case '\b':
        *out += "\\b";
        break;
      case '\f':
        *out += "\\f";
        break;
      case '\n':
        *out += "\\n";
        break;
      case '\r':
        *out += "\\r";
        break;
      case '\t':
        *out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          *out += buf;
        } else {
          out->push_back(static_cast<char>(c));
        }
    }
  }
  out->push_back('"');
}

void DumpValue(const JsonValue& v, std::string* out) {
  switch (v.type()) {
    case JsonValue::Type::kNull:
      *out += "null";
      break;
    case JsonValue::Type::kBool:
      *out += v.as_bool() ? "true" : "false";
      break;
    case JsonValue::Type::kNumber: {
      double n = v.as_number();
      if (std::isfinite(n) && n == static_cast<long long>(n) &&
          std::fabs(n) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
        *out += buf;
      } else {
        std::ostringstream oss;
        oss.precision(17);
        oss << n;
        *out += oss.str();
      }
      break;
    }
    case JsonValue::Type::kString:
      EscapeInto(v.as_string(), out);
      break;
    case JsonValue::Type::kArray: {
      *out += '[';
      bool first = true;
      for (size_t i = 0; i < v.size(); ++i) {
        if (!first)
          *out += ',';
        first = false;
        const JsonPtr& item = v.at(i);
        if (item)
          DumpValue(*item, out);
        else
          *out += "null";
      }
      *out += ']';
      break;
    }
    case JsonValue::Type::kObject: {
      *out += '{';
      bool first = true;
      for (const auto& kv : v.RawMembers()) {
        if (!first)
          *out += ',';
        first = false;
        EscapeInto(kv.first, out);
        *out += ':';
        if (kv.second)
          DumpValue(*kv.second, out);
        else
          *out += "null";
      }
      *out += '}';
      break;
    }
  }
}

}  // namespace

namespace {

// Sentinel for missing keys / out-of-range access: a non-owning JsonPtr to a
// static null JsonValue. Accessors can then be called safely (they report
// the null type) instead of dereferencing an empty shared_ptr.
const JsonPtr& NullSentinel() {
  static JsonValue value;
  static JsonPtr ptr(&value, [](JsonValue*) {});
  return ptr;
}

}  // namespace

const JsonPtr& JsonValue::get(const std::string& key) const {
  if (type_ != Type::kObject)
    return NullSentinel();
  auto it = object_.find(key);
  return it == object_.end() ? NullSentinel() : it->second;
}

const JsonPtr& JsonValue::at(size_t index) const {
  if (type_ != Type::kArray || index >= array_.size())
    return NullSentinel();
  return array_[index];
}

void JsonValue::set(const std::string& key, JsonPtr value) {
  if (type_ != Type::kObject)
    return;
  object_[key] = std::move(value);
}

void JsonValue::push(JsonPtr value) {
  if (type_ != Type::kArray)
    return;
  array_.push_back(std::move(value));
}

const std::map<std::string, JsonPtr>& JsonValue::RawMembers() const {
  return object_;
}

std::string JsonValue::Dump() const {
  std::string out;
  DumpValue(*this, &out);
  return out;
}

JsonPtr JsonParse(const std::string& text, std::string* error) {
  Parser parser(text);
  return parser.Run(error);
}

}  // namespace weasel_ai
