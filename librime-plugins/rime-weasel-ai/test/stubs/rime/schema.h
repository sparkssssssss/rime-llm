#pragma once
#include <rime/common.h>
namespace rime {
class Config;
class Schema {
 public:
  Schema() = default;
  Config* config() const { return config_; }
  void set_config(Config* c) { config_ = c; }
  int page_size() const { return page_size_; }
  void set_page_size(int n) { page_size_ = n; }
  const string& schema_id() const { return schema_id_; }
 private:
  Config* config_ = nullptr;
  int page_size_ = 5;
  string schema_id_ = "test";
};
}  // namespace rime
