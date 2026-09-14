#pragma once
#include <rime/common.h>
namespace rime {
class Context {
 public:
  const string& input() const { return input_; }
  void set_input(const string& v) { input_ = v; }
 private:
  string input_;
};
}  // namespace rime
