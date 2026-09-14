#pragma once
#include <rime/common.h>
namespace rime {
class Engine;
class Schema;
struct Ticket {
  Engine* engine = nullptr;
  Schema* schema = nullptr;
  string name_space;
};
}  // namespace rime
