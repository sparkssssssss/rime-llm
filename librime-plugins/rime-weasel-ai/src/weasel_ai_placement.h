// Weasel AI correction - AI candidate placement planner.
//
// Pure logic, no Rime types: decides, for a streaming emission, whether the
// next candidate should come from the upstream translation or be an AI
// candidate. Kept separate so it can be unit tested (see test/).
//
// Semantics:
//   * insert_index == kLast      -> AI candidates are emitted after all
//                                   upstream candidates (appended last)
//   * insert_index == N          -> the first AI candidate is emitted as the
//                                   N-th output (0-based), i.e. right after
//                                   N upstream candidates; further AI
//                                   candidates follow at the end

#ifndef WEASEL_AI_PLACEMENT_H_
#define WEASEL_AI_PLACEMENT_H_

#include <cstddef>

namespace weasel_ai {

enum class EmitAction {
  kUpstream,  // pull the next candidate from the upstream translation
  kAi,        // emit the next AI candidate
  kDone,      // nothing left
};

class AiPlacement {
 public:
  static constexpr size_t kLast = static_cast<size_t>(-1);

  AiPlacement(size_t insert_index, size_t ai_count)
      : insert_index_(insert_index), ai_count_(ai_count) {}

  // `emitted` = number of candidates already emitted.
  // `upstream_exhausted` = upstream has no more candidates.
  EmitAction Next(size_t emitted, bool upstream_exhausted) {
    if (ai_emitted_ < ai_count_) {
      if (emitted == insert_index_ || upstream_exhausted) {
        ++ai_emitted_;
        return EmitAction::kAi;
      }
    }
    if (upstream_exhausted)
      return EmitAction::kDone;
    return EmitAction::kUpstream;
  }

  size_t ai_emitted() const { return ai_emitted_; }
  size_t ai_count() const { return ai_count_; }

 private:
  size_t insert_index_;
  size_t ai_count_;
  size_t ai_emitted_ = 0;
};

}  // namespace weasel_ai

#endif  // WEASEL_AI_PLACEMENT_H_
