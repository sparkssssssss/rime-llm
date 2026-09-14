#pragma once
#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/ticket.h>
#include <rime/translation.h>
namespace rime {
class Filter {
 public:
  explicit Filter(const Ticket& ticket)
      : engine_(ticket.engine), name_space_(ticket.name_space) {}
  virtual ~Filter() = default;
  virtual an<Translation> Apply(an<Translation> translation,
                                CandidateList* candidates) = 0;
 protected:
  Engine* engine_;
  string name_space_;
};
}  // namespace rime
