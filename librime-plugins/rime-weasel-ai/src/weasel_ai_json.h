// Weasel AI correction - minimal JSON value model and parser.
//
// Self-contained: librime has no JSON dependency, and the plugin must not
// pull in new third-party libraries to keep the merged Windows build simple.
// Supports exactly what the OpenAI-compatible chat completion exchange
// needs: objects, arrays, strings (with escapes), numbers, booleans, null.
//
// Content strings are assumed to be UTF-8. \uXXXX escapes are decoded to
// UTF-8. Invalid escape sequences fail parsing (never silently dropped).

#ifndef WEASEL_AI_JSON_H_
#define WEASEL_AI_JSON_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace weasel_ai {

class JsonValue;
using JsonPtr = std::shared_ptr<JsonValue>;

class JsonValue {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  JsonValue() : type_(Type::kNull) {}
  explicit JsonValue(bool b) : type_(Type::kBool), bool_(b) {}
  explicit JsonValue(double n) : type_(Type::kNumber), number_(n) {}
  explicit JsonValue(std::string s)
      : type_(Type::kString), string_(std::move(s)) {}

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::kNull; }
  bool is_object() const { return type_ == Type::kObject; }
  bool is_array() const { return type_ == Type::kArray; }
  bool is_string() const { return type_ == Type::kString; }
  bool is_number() const { return type_ == Type::kNumber; }
  bool is_bool() const { return type_ == Type::kBool; }

  bool as_bool(bool def = false) const {
    return type_ == Type::kBool ? bool_ : def;
  }
  double as_number(double def = 0.0) const {
    return type_ == Type::kNumber ? number_ : def;
  }
  const std::string& as_string() const { return string_; }

  // Object access. Returns null JsonPtr when missing or not an object.
  const JsonPtr& get(const std::string& key) const;
  // Array access. Returns null JsonPtr when out of range or not an array.
  const JsonPtr& at(size_t index) const;
  size_t size() const {
    return type_ == Type::kArray
               ? array_.size()
               : (type_ == Type::kObject ? object_.size() : 0);
  }

  // builders (used by tests and request serialization)
  void set_object() {
    type_ = Type::kObject;
    object_.clear();
  }
  void set_array() {
    type_ = Type::kArray;
    array_.clear();
  }
  void set(const std::string& key, JsonPtr value);
  void push(JsonPtr value);
  void set_string(std::string s) {
    type_ = Type::kString;
    string_ = std::move(s);
  }

  static JsonPtr MakeObject() {
    auto v = std::make_shared<JsonValue>();
    v->set_object();
    return v;
  }
  static JsonPtr MakeArray() {
    auto v = std::make_shared<JsonValue>();
    v->set_array();
    return v;
  }
  static JsonPtr MakeString(std::string s) {
    return std::make_shared<JsonValue>(std::move(s));
  }
  static JsonPtr MakeNumber(double n) {
    return std::make_shared<JsonValue>(n);
  }
  static JsonPtr MakeBool(bool b) {
    return std::make_shared<JsonValue>(b);
  }

  // Serialize back to compact JSON (request bodies). Strings are escaped.
  std::string Dump() const;

  const std::map<std::string, JsonPtr>& RawMembers() const;

 private:
  Type type_;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<JsonPtr> array_;
  std::map<std::string, JsonPtr> object_;
};

// Parses `text`. On success returns the root and sets *error to empty.
// On failure returns a null JsonPtr and sets *error to a short reason.
JsonPtr JsonParse(const std::string& text, std::string* error);

}  // namespace weasel_ai

#endif  // WEASEL_AI_JSON_H_
