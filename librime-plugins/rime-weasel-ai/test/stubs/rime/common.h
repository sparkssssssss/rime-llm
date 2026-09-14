// Minimal stand-ins for librime headers, used ONLY by local logic tests.
#pragma once
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#define LOG(sev) std::cerr

namespace rime {
using std::string;
template <class T>
using an = std::shared_ptr<T>;
template <class T>
using the = std::unique_ptr<T>;
template <class T, class... Args>
an<T> New(Args&&... args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
}
}  // namespace rime
