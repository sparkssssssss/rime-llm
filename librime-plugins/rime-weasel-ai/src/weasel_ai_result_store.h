// Weasel AI correction - per-engine result store.
//
// The Processor (trigger) writes; the Translator/Filter (query) read. All
// access happens on the session's own thread, so the store itself is not
// locked; the registry that hands it out is (see store_registry.h).
//
// The generation counter is the request generation: every trigger bumps it,
// and a result is only committed when its token is still current, so a
// stale in-flight response can never overwrite a newer one.

#ifndef WEASEL_AI_RESULT_STORE_H_
#define WEASEL_AI_RESULT_STORE_H_

#include <string>
#include <vector>

#include "weasel_ai_correction_service.h"

#include <rime/common.h>

namespace weasel_ai {

struct AiResult {
  std::string input;  // segment input the result was produced for
  std::vector<AiCandidate> candidates;
  // Segment range at trigger time, so the filter can build candidates
  // without reaching into the upstream translation.
  size_t seg_start = 0;
  size_t seg_end = 0;
  bool has_range = false;
};

class AiResultStore {
 public:
  // Bumps the generation and returns the new token.
  int BeginRequest() { return ++generation_; }

  // Stores a result only if `token` is still current. Returns true on keep.
  bool Commit(int token, AiResult result) {
    if (token != generation_)
      return false;
    result_ = std::move(result);
    has_result_ = !result_.candidates.empty();
    ai_index_ = kNoIndex;
    return has_result_;
  }

  void Invalidate() {
    has_result_ = false;
    result_ = AiResult();
    ai_index_ = kNoIndex;
    ++generation_;  // everything outstanding becomes stale
  }

  // Returns the stored result if it matches `input`, otherwise nullptr.
  const AiResult* Match(const std::string& input) const {
    if (!has_result_ || result_.input != input)
      return nullptr;
    return &result_;
  }

  bool HasResult() const { return has_result_; }

  // Returns the stored result regardless of its input key (callers that
  // want to compare against a reconstructed segment input).
  const AiResult* MatchAny() const { return has_result_ ? &result_ : nullptr; }

  // Absolute menu index where the filter placed the AI candidate, or
  // kNoIndex. Written by the filter, read by the processor to highlight the
  // candidate without scanning the menu.
  static constexpr size_t kNoIndex = static_cast<size_t>(-1);
  void set_ai_index(size_t index) { ai_index_ = index; }
  size_t ai_index() const { return ai_index_; }
  void clear_ai_index() { ai_index_ = kNoIndex; }

 private:
  int generation_ = 0;
  bool has_result_ = false;
  size_t ai_index_ = kNoIndex;
  AiResult result_;
};

}  // namespace weasel_ai

#endif  // WEASEL_AI_RESULT_STORE_H_
