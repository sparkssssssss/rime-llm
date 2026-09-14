// Weasel AI correction - correction service client.
//
// Builds an OpenAI-compatible chat completion request from the current
// input/candidates/context, posts it, parses choices[0].message.content as
// JSON {"candidates":[{"text":..., "score":...}]}, and applies validation:
// UTF-8 cleanliness, control-character rejection, length limits and count
// limits. All failures degrade to "no AI candidates" - never an exception
// thrown across the Rime boundary.

#ifndef WEASEL_AI_CORRECTION_SERVICE_H_
#define WEASEL_AI_CORRECTION_SERVICE_H_

#include <string>
#include <vector>

#include "weasel_ai_config.h"

namespace weasel_ai {

// Default per-candidate byte cap for AI output.
constexpr size_t kDefaultMaxCandidateBytes = 512;

struct AiCandidate {
  std::string text;
  double score = 0.0;
};

struct CorrectionRequest {
  std::string input;                        // raw pinyin of current segment
  std::vector<std::string> candidates;      // existing menu candidates
  std::string committed_context;            // recent committed text (may be empty)
};

struct CorrectionResponse {
  bool ok = false;          // request completed and at least one candidate parsed
  std::string error;        // machine-readable reason when !ok
  std::vector<AiCandidate> candidates;
  // rerank mode: index the model picked inside the submitted candidate list
  // (-1 when not applicable / unparseable).
  int picked_index = -1;
};

class CorrectionService {
 public:
  explicit CorrectionService(const AiCorrectionConfig& config);

  // Performs the blocking request. Call only from explicit user trigger.
  CorrectionResponse Correct(const CorrectionRequest& request) const;

  // Builds the JSON body (exposed for tests). include_reasoning_effort=false
  // omits the field, used when a gateway rejects it.
  std::string BuildRequestBody(const CorrectionRequest& request,
                               bool include_reasoning_effort = true) const;

  // Parses a chat-completion response body (exposed for tests).
  static CorrectionResponse ParseResponseBody(
      const std::string& body,
      size_t max_candidates,
      size_t max_candidate_bytes = kDefaultMaxCandidateBytes);

  // Parses a rerank reply: {"index": k, "text": "..."} (text optional).
  static int ParseRerankIndex(const std::string& body);

 private:
  AiCorrectionConfig config_;
};

// Rejects strings that contain control characters or invalid UTF-8.
bool IsSafeCandidateText(const std::string& text, size_t max_bytes);

}  // namespace weasel_ai

#endif  // WEASEL_AI_CORRECTION_SERVICE_H_
