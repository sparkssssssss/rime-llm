#pragma once
#include <map>
#include <rime/common.h>
namespace rime {
// Value-bag fake. The test pre-populates the shared "default" config.
class Config {
 public:
  bool GetString(const string& path, string* value) const {
    auto it = values_.find(path);
    if (it == values_.end()) return false;
    *value = it->second;
    return true;
  }
  bool GetInt(const string& path, int* value) const {
    auto it = values_.find(path);
    if (it == values_.end()) return false;
    *value = std::stoi(it->second);
    return true;
  }
  bool GetBool(const string& path, bool* value) const {
    auto it = values_.find(path);
    if (it == values_.end()) return false;
    *value = (it->second == "1" || it->second == "true");
    return true;
  }
  bool GetDouble(const string& path, double* value) const {
    auto it = values_.find(path);
    if (it == values_.end()) return false;
    *value = std::stod(it->second);
    return true;
  }
  void Set(const string& path, const string& value) { values_[path] = value; }
  void Clear() { values_.clear(); }

  struct Component {
    // librime's real Create() returns a fresh heap object owned by the
    // caller (wrapped in the<Config>); mirror that ownership.
    Config* Create(const string&) {
      Config* created = new Config();
      *created = Config::Deployed();
      return created;
    }
  };
  static Component* Require(const string&) {
    static Component component;
    return &component;
  }
  // shared config that stands in for the deployed default.yaml
  static Config& Deployed() {
    static Config config;
    return config;
  }

 private:
  std::map<string, string> values_;
};
}  // namespace rime
