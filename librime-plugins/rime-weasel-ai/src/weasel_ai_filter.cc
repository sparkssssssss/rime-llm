#include "weasel_ai_filter.h"

#include <rime/candidate.h>
#include <rime/config.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/translation.h>

namespace weasel_ai {

namespace {

// Materializes the upstream translation, partitions it into normal and
// ai_correction queues, then replays: normal candidates first (original
// order), AI candidates last (deduplicated against everything emitted).
class AiLastTranslation : public rime::Translation {
 public:
  AiLastTranslation(rime::an<rime::Translation> translation,
                    size_t insert_index)
      : insert_index_(insert_index) {
    while (!translation->exhausted()) {
      auto cand = translation->Peek();
      if (!cand)
        break;
      if (cand->type() == "ai_correction") {
        ai_.push_back(cand);
      } else {
        normal_.push_back(cand);
      }
      translation->Next();
    }
    LOG(INFO) << "[weasel_ai] filter: normal=" << normal_.size()
              << " ai=" << ai_.size();
    if (normal_.empty() && ai_.empty())
      set_exhausted(true);
  }

  bool Next() override {
    if (exhausted())
      return false;
    Advance();
    return !exhausted();
  }

  rime::an<rime::Candidate> Peek() override {
    if (exhausted())
      return nullptr;
    if (!current_ && !advanced_)
      Advance();
    return current_;
  }

 private:
  void Advance() {
    current_.reset();
    advanced_ = true;
    // AI candidates are inserted at absolute output index insert_index_;
    // static_cast<size_t>(-1) means very end.
    if (ai_cursor_ < ai_.size() && emitted_count_ == insert_index_) {
      auto cand = ai_[ai_cursor_++];
      if (!IsDuplicate(cand->text())) {
        emitted_.push_back(cand->text());
        current_ = cand;
        ++emitted_count_;
        set_exhausted(false);
        return;
      }
    }
    if (cursor_ < normal_.size()) {
      current_ = normal_[cursor_++];
      ++emitted_count_;
      set_exhausted(false);
      return;
    }
    while (ai_cursor_ < ai_.size()) {
      auto cand = ai_[ai_cursor_++];
      if (IsDuplicate(cand->text()))
        continue;
      emitted_.push_back(cand->text());
      current_ = cand;
      ++emitted_count_;
      set_exhausted(false);
      return;
    }
    set_exhausted(true);
  }

  bool IsDuplicate(const rime::string& text) const {
    for (const auto& emitted : emitted_) {
      if (emitted == text)
        return true;
    }
    return false;
  }

  rime::CandidateList normal_;
  rime::CandidateList ai_;
  rime::CandidateList::size_type cursor_ = 0;
  rime::CandidateList::size_type ai_cursor_ = 0;
  size_t insert_index_ = static_cast<size_t>(-1);
  size_t emitted_count_ = 0;
  std::vector<rime::string> emitted_;
  rime::an<rime::Candidate> current_;
  bool advanced_ = false;
};

}  // namespace

AiCorrectionFilter::AiCorrectionFilter(const rime::Ticket& ticket)
    : Filter(ticket) {
  // placement: "last" (default) or "page1_end"
  std::string position;
  int page_size = 5;
  if (engine_ && engine_->schema()) {
    rime::Config* config = engine_->schema()->config();
    if (config) {
      if (!config->GetString("ai_correction/candidate_position", &position)) {
        rime::the<rime::Config> global(
            rime::Config::Require("config")->Create("default"));
        if (global)
          global->GetString("ai_correction/candidate_position", &position);
      }
      if (!config->GetInt("ai_correction/page_size", &page_size)) {
        rime::the<rime::Config> global(
            rime::Config::Require("config")->Create("default"));
        if (global)
          global->GetInt("ai_correction/page_size", &page_size);
      }
    }
  }
  if (page_size < 1)
    page_size = 1;
  insert_index_ = (position == "page1_end") ? static_cast<size_t>(page_size - 1)
                                            : static_cast<size_t>(-1);
  LOG(INFO) << "[weasel_ai] filter placement=" << position
            << " insert_index=" << insert_index_;
}

rime::an<rime::Translation> AiCorrectionFilter::Apply(
    rime::an<rime::Translation> translation,
    rime::CandidateList* candidates) {
  auto filtered = rime::New<AiLastTranslation>(translation, insert_index_);
  if (filtered->exhausted())
    return nullptr;
  return filtered;
}

}  // namespace weasel_ai
