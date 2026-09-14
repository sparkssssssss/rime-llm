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
                      AiResultStore* store,
                      bool deduplicate)
      : upstream_(upstream),
        ai_(std::move(ai_candidates)),
        placement_(insert_index, ai_.size()),
        store_(store),
        deduplicate_(deduplicate) {
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
      const bool upstream_exhausted =
          upstream_done_ || !upstream_ || upstream_->exhausted();
      switch (placement_.Next(emitted_, upstream_exhausted)) {
        case EmitAction::kAi: {
          // placement_ already advanced its cursor; take the next AI item.
          const size_t ai_pos = placement_.ai_emitted() - 1;
          if (ai_pos >= ai_.size())
            continue;
          auto cand = ai_[ai_pos];
          if (deduplicate_ && IsDuplicateText(cand->text()))
            continue;  // configured to hide duplicates; keep planning
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
            // Upstream claims non-exhausted but yields nothing: give it a
            // few chances, then treat it as empty so we can never spin.
            if (upstream_)
              upstream_->Next();
            if (++null_pulls_ > 8)
              upstream_done_ = true;
            continue;
          }
          null_pulls_ = 0;
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
  bool deduplicate_ = false;
  std::vector<rime::string> seen_texts_;
  rime::an<rime::Candidate> current_;
  size_t emitted_ = 0;
  int null_pulls_ = 0;
  bool upstream_done_ = false;
};

struct FilterConfig {
  std::string position = "last";
  int page_size = 5;
  bool deduplicate = false;
};

// Reads ai_correction/* with schema-level values taking precedence over the
// deployed default config. Every key uses an explicit "found" flag so a
// schema value of false/0 is not overridden by the global one.
FilterConfig ReadFilterConfig(rime::Engine* engine) {
  FilterConfig result;
  rime::Config* schema_config =
      (engine && engine->schema()) ? engine->schema()->config() : nullptr;
  rime::the<rime::Config> global(
      rime::Config::Require("config")->Create("default"));

  bool found = false;
  found = schema_config &&
          schema_config->GetString("ai_correction/candidate_position",
                                   &result.position);
  if (!found && global)
    global->GetString("ai_correction/candidate_position", &result.position);
  if (result.position.empty())
    result.position = "last";

  int size = 0;
  found = schema_config &&
          schema_config->GetInt("ai_correction/page_size", &size);
  if (!found && global)
    found = global->GetInt("ai_correction/page_size", &size);
  if (!found || size <= 0) {
    // Schema::page_size() is authoritative: librime's DefaultConfigPlugin
    // injects default.yaml's `menu` section into every schema config, and
    // rime_api's get_context() paginates with exactly this value.
    size = (engine && engine->schema()) ? engine->schema()->page_size() : 5;
  }
  result.page_size = size < 1 ? 1 : size;

  found = schema_config &&
          schema_config->GetBool("ai_correction/deduplicate",
                                 &result.deduplicate);
  if (!found && global)
    global->GetBool("ai_correction/deduplicate", &result.deduplicate);
  return result;
}

}  // namespace

AiCorrectionFilter::AiCorrectionFilter(const rime::Ticket& ticket)
    : Filter(ticket) {
  store_ = FindOrCreateStore(ticket.engine);
  const FilterConfig config = ReadFilterConfig(engine_);
  deduplicate_ = config.deduplicate;
  insert_index_ = (config.position == "page1_end")
                      ? static_cast<size_t>(config.page_size - 1)
                      : AiPlacement::kLast;
  LOG(INFO) << "[weasel_ai] filter placement=" << config.position
            << " page_size=" << config.page_size
            << " insert_index=" << insert_index_
            << " deduplicate=" << deduplicate_;
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
  const std::string comment =
      result->comment.empty() ? std::string("AI校准") : result->comment;
  for (const auto& cand : result->candidates) {
    ai_candidates.push_back(rime::New<rime::SimpleCandidate>(
        "ai_correction", start, end, cand.text, comment));
  }

  LOG(INFO) << "[weasel_ai] filter inserting " << ai_candidates.size()
            << " AI candidate(s) at index " << insert_index_
            << " (range " << start << "-" << end << ")";
  store_->clear_ai_index();
  auto filtered = rime::New<AiInsertTranslation>(
      translation, std::move(ai_candidates), insert_index_, store_,
      deduplicate_);
  if (filtered->exhausted())
    return translation;
  return filtered;
}

}  // namespace weasel_ai
