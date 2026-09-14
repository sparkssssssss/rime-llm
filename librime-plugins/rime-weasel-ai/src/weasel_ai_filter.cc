#include "weasel_ai_filter.h"

#include <rime/candidate.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/translation.h>

#include "weasel_ai_placement.h"
#include "weasel_ai_store_registry.h"

namespace weasel_ai {

namespace {

// Lazy insertion translation: streams upstream candidates, inserts the AI
// candidates at the planned position, and drops upstream AI duplicates.
class AiInsertTranslation : public rime::Translation {
 public:
  AiInsertTranslation(rime::an<rime::Translation> upstream,
                      std::vector<rime::an<rime::Candidate>> ai_candidates,
                      size_t insert_index,
                      AiResultStore* store)
      : upstream_(upstream),
        ai_(std::move(ai_candidates)),
        placement_(insert_index, ai_.size()),
        store_(store) {
    if ((!upstream_ || upstream_->exhausted()) && ai_.empty())
      set_exhausted(true);
  }

  bool Next() override {
    if (exhausted())
      return false;
    current_.reset();
    Advance();
    return !exhausted();
  }

  rime::an<rime::Candidate> Peek() override {
    if (exhausted())
      return nullptr;
    if (!current_)
      Advance();
    return current_;
  }

 private:
  void Advance() {
    current_.reset();
    for (;;) {
      const bool upstream_exhausted = !upstream_ || upstream_->exhausted();
      switch (placement_.Next(emitted_, upstream_exhausted)) {
        case EmitAction::kAi: {
          // placement_ already advanced its cursor; take the next AI item.
          const size_t ai_pos = placement_.ai_emitted() - 1;
          if (ai_pos >= ai_.size())
            continue;
          auto cand = ai_[ai_pos];
          if (IsDuplicateText(cand->text()))
            continue;  // skip duplicate, keep planning
          if (store_)
            store_->set_ai_index(emitted_);
          seen_texts_.push_back(cand->text());
          current_ = cand;
          ++emitted_;
          set_exhausted(false);
          return;
        }
        case EmitAction::kUpstream: {
          auto cand = upstream_ ? upstream_->Peek() : nullptr;
          if (!cand) {
            // Upstream lied about being non-exhausted; treat as empty.
            if (upstream_)
              upstream_->Next();
            continue;
          }
          upstream_->Next();
          // Upstream AI candidates are re-created by this filter.
          if (cand->type() == "ai_correction")
            continue;
          seen_texts_.push_back(cand->text());
          current_ = cand;
          ++emitted_;
          set_exhausted(false);
          return;
        }
        case EmitAction::kDone:
          set_exhausted(true);
          return;
      }
    }
  }

  bool IsDuplicateText(const rime::string& text) const {
    for (const auto& seen : seen_texts_) {
      if (seen == text)
        return true;
    }
    return false;
  }

  rime::an<rime::Translation> upstream_;
  std::vector<rime::an<rime::Candidate>> ai_;
  AiPlacement placement_;
  AiResultStore* store_ = nullptr;
  std::vector<rime::string> seen_texts_;
  rime::an<rime::Candidate> current_;
  size_t emitted_ = 0;
};

std::string ReadPlacementConfig(rime::Engine* engine, int* page_size) {
  std::string position = "last";
  int size = 0;
  rime::Config* config =
      (engine && engine->schema()) ? engine->schema()->config() : nullptr;
  if (config) {
    config->GetString("ai_correction/candidate_position", &position);
    config->GetInt("ai_correction/page_size", &size);
  }
  if (size <= 0) {
    // Prefer the schema's real page size; fall back to the global default.
    size = (engine && engine->schema()) ? engine->schema()->page_size() : 5;
    if (config) {
      rime::the<rime::Config> global(
          rime::Config::Require("config")->Create("default"));
      if (global)
        global->GetInt("menu/page_size", &size);
    }
  }
  if (size < 1)
    size = 1;
  if (page_size)
    *page_size = size;
  return position;
}

}  // namespace

AiCorrectionFilter::AiCorrectionFilter(const rime::Ticket& ticket)
    : Filter(ticket) {
  store_ = FindOrCreateStore(ticket.engine);
  int page_size = 5;
  const std::string position = ReadPlacementConfig(engine_, &page_size);
  insert_index_ = (position == "page1_end")
                      ? static_cast<size_t>(page_size - 1)
                      : AiPlacement::kLast;
  LOG(INFO) << "[weasel_ai] filter placement=" << position
            << " page_size=" << page_size
            << " insert_index=" << insert_index_;
}

rime::an<rime::Translation> AiCorrectionFilter::Apply(
    rime::an<rime::Translation> translation,
    rime::CandidateList* candidates) {
  // Fast path: nothing pending -> hand the upstream translation through
  // untouched. This keeps the typing path fully lazy.
  if (!store_ || !engine_ || !engine_->context())
    return translation;
  // Match on the same key the translator used: the stored result covers a
  // segment range, so compare that substring rather than the whole input
  // (they differ when the composition has multiple segments).
  const std::string& full_input = engine_->context()->input();
  const AiResult* result = store_->Match(full_input);
  if (!result && store_->HasResult()) {
    // Reconstruct the segment input from the recorded range.
    const AiResult* any = store_->MatchAny();
    if (any && any->has_range && any->seg_end <= full_input.size() &&
        any->seg_end > any->seg_start &&
        full_input.substr(any->seg_start, any->seg_end - any->seg_start) ==
            any->input) {
      result = any;
    }
  }
  if (!result)
    return translation;

  // Build the AI candidates from the stored result; the segment range was
  // captured at trigger time so no upstream inspection is needed.
  std::vector<rime::an<rime::Candidate>> ai_candidates;
  const size_t start = result->seg_start;
  const size_t end = result->seg_end;
  for (const auto& cand : result->candidates) {
    ai_candidates.push_back(rime::New<rime::SimpleCandidate>(
        "ai_correction", start, end, cand.text, "AI校准"));
  }

  store_->clear_ai_index();
  auto filtered = rime::New<AiInsertTranslation>(
      translation, std::move(ai_candidates), insert_index_, store_);
  if (filtered->exhausted())
    return translation;
  return filtered;
}

}  // namespace weasel_ai
