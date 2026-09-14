#include "weasel_ai_translator.h"

#include <rime/candidate.h>
#include <rime/config.h>
#include <rime/engine.h>
#include <rime/schema.h>

#include "weasel_ai_store_registry.h"

namespace weasel_ai {

AiCorrectionTranslator::AiCorrectionTranslator(const rime::Ticket& ticket)
    : Translator(ticket) {
  store_.reset(FindOrCreateStore(ticket.engine));
  // comment label is configurable; schema config first, then default config
  comment_ = "AI校准";
  if (engine_ && engine_->schema()) {
    rime::Config* config = engine_->schema()->config();
    if (config && !config->GetString("ai_correction/comment", &comment_)) {
      rime::the<rime::Config> global(
          rime::Config::Require("config")->Create("default"));
      if (global)
        global->GetString("ai_correction/comment", &comment_);
    }
  }
}

rime::an<rime::Translation> AiCorrectionTranslator::Query(
    const rime::string& input,
    const rime::Segment& segment) {
  if (!segment.HasTag("abc"))
    return nullptr;
  const AiResult* result = store_->Match(input);
  if (!result) {
    return nullptr;
  }
  auto translation = rime::New<rime::FifoTranslation>();
  const size_t start = result->has_range ? result->seg_start : segment.start;
  const size_t end = result->has_range ? result->seg_end : segment.end;
  for (const auto& cand : result->candidates) {
    translation->Append(rime::New<rime::SimpleCandidate>(
        "ai_correction", start, end, cand.text, comment_));
  }
  return translation;
}

}  // namespace weasel_ai
