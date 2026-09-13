// Weasel AI correction - filter that pushes ai_correction candidates to the
// END of the candidate list and drops duplicates of existing candidates.
//
// Why a filter: MergedTranslation elects candidates by
// Candidate::compare (start, then longer end, then quality), so a same-range
// AI candidate with low quality would interleave with normal candidates
// rather than reliably land last. Reordering in the filter pass is
// deterministic and independent of other translators' quality values.

#ifndef WEASEL_AI_FILTER_H_
#define WEASEL_AI_FILTER_H_

#include <rime/filter.h>

namespace weasel_ai {

class AiCorrectionFilter : public rime::Filter {
 public:
  explicit AiCorrectionFilter(const rime::Ticket& ticket);

  rime::an<rime::Translation> Apply(
      rime::an<rime::Translation> translation,
      rime::CandidateList* candidates) override;
};

}  // namespace weasel_ai

#endif  // WEASEL_AI_FILTER_H_
