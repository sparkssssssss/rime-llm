// Weasel AI correction - filter that places the AI candidate and keeps the
// candidate stream lazy.
//
// Design notes (see code review):
//   * When no AI result is pending for the current input the upstream
//     translation is returned UNCHANGED - zero overhead on the typing path.
//   * With a pending result the filter streams candidates lazily and inserts
//     the AI candidate at the configured position. It never materializes the
//     whole translation (rime_ice can produce thousands of candidates).
//   * The AI candidates come from the per-engine store (with segment range
//     recorded at trigger time); upstream ai_correction candidates are
//     skipped to avoid duplicates.

#ifndef WEASEL_AI_FILTER_H_
#define WEASEL_AI_FILTER_H_

#include <rime/filter.h>

#include "weasel_ai_placement.h"
#include "weasel_ai_result_store.h"

namespace weasel_ai {

class AiCorrectionFilter : public rime::Filter {
 public:
  explicit AiCorrectionFilter(const rime::Ticket& ticket);

  rime::an<rime::Translation> Apply(
      rime::an<rime::Translation> translation,
      rime::CandidateList* candidates) override;

 private:
  // Absolute output index for the first AI candidate (AiPlacement::kLast for
  // "append at the end").
  size_t insert_index_ = AiPlacement::kLast;
  bool deduplicate_ = false;
  AiResultStore* store_ = nullptr;
};

}  // namespace weasel_ai

#endif  // WEASEL_AI_FILTER_H_
