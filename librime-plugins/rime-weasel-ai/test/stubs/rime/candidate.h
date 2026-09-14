#pragma once
#include <rime/common.h>
namespace rime {
class Candidate {
 public:
  Candidate(const string& type, size_t start, size_t end)
      : type_(type), start_(start), end_(end) {}
  virtual ~Candidate() = default;
  const string& type() const { return type_; }
  virtual const string& text() const { return text_; }
  virtual string comment() const { return comment_; }
  size_t start() const { return start_; }
  size_t end() const { return end_; }
  double quality() const { return quality_; }
 protected:
  string type_, text_, comment_;
  size_t start_, end_;
  double quality_ = 0.0;
};
class SimpleCandidate : public Candidate {
 public:
  SimpleCandidate(const string& type, size_t start, size_t end,
                  const string& text, const string& comment = string())
      : Candidate(type, start, end) {
    text_ = text;
    comment_ = comment;
  }
};
using CandidateList = std::vector<an<Candidate>>;
}  // namespace rime
