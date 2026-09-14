#pragma once
#include <rime/common.h>
namespace rime {
class Schema;
class Context;
class Engine {
 public:
  Schema* schema() const { return schema_; }
  void set_schema(Schema* s) { schema_ = s; }
  Context* context() const { return context_; }
  void set_context(Context* c) { context_ = c; }
 private:
  Schema* schema_ = nullptr;
  Context* context_ = nullptr;
};
}  // namespace rime
