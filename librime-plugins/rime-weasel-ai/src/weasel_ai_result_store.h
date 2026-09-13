// Weasel AI correction - shared per-engine result store.
//
// The Processor (trigger) writes; the Translator (query) reads. Both live in
// the same engine thread in the MVP synchronous flow, so a plain struct with
// a version counter is sufficient. The version counter is the request
// generation: every trigger bumps it, and stale results are dropped by
// comparing the version captured at trigger time with the version at result
// apply time.

#ifndef WEASEL_AI_RESULT_STORE_H_
#define WEASEL_AI_RESULT_STORE_H_

#include <string>
#include <vector>

#include "weasel_ai_correction_service.h"

#include <rime/common.h>

namespace weasel_ai {

struct AiResult {
  std::string input;              // input the result was produced for
  std::vector<AiCandidate> candidates;
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
    return has_result_;
  }

  void Invalidate() {
    has_result_ = false;
    result_ = AiResult();
    ++generation_;  // everything outstanding becomes stale
  }

  // Returns the stored result if it matches `input`, otherwise nullptr.
  const AiResult* Match(const std::string& input) const {
    if (!has_result_ || result_.input != input)
      return nullptr;
    return &result_;
  }

 private:
  int generation_ = 0;
  bool has_result_ = false;
  AiResult result_;
};

}  // namespace weasel_ai

#endif  // WEASEL_AI_RESULT_STORE_H_
