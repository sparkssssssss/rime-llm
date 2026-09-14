#include "weasel_ai_translator.h"

#include <rime/candidate.h>

#include "weasel_ai_store_registry.h"

namespace weasel_ai {

AiCorrectionTranslator::AiCorrectionTranslator(const rime::Ticket& ticket)
    : Translator(ticket) {
  store_.reset(FindOrCreateStore(ticket.engine));
}

rime::an<rime::Translation> AiCorrectionTranslator::Query(
    const rime::string& input,
    const rime::Segment& segment) {
  if (!segment.HasTag("abc"))
    return nullptr;
  const AiResult* result = store_->Match(input);
  if (!result) {
    static thread_local int quiet = 0;
    if (++quiet % 50 == 1)
      LOG(INFO) << "[weasel_ai] translator: no stored result for input";
    return nullptr;
  }
  LOG(INFO) << "[weasel_ai] translator: serving " << result->candidates.size()
            << " stored candidates for input";
  auto translation = rime::New<rime::FifoTranslation>();
  for (const auto& cand : result->candidates) {
    auto candidate = rime::New<rime::SimpleCandidate>(
        "ai_correction", segment.start, segment.end, cand.text, "AI校准");
    translation->Append(candidate);
  }
  return translation;
}

}  // namespace weasel_ai
