#pragma once
#include <rime/candidate.h>
namespace rime {
class Translation {
 public:
  virtual ~Translation() = default;
  virtual bool Next() = 0;
  virtual an<Candidate> Peek() = 0;
  bool exhausted() const { return exhausted_; }
 protected:
  void set_exhausted(bool v) { exhausted_ = v; }
 private:
  bool exhausted_ = false;
};
}  // namespace rime
