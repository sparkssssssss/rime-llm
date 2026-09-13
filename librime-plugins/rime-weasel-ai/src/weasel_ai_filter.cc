#include "weasel_ai_filter.h"

#include <rime/candidate.h>
#include <rime/translation.h>

namespace weasel_ai {

namespace {

// Materializes the upstream translation, partitions it into normal and
// ai_correction queues, then replays: normal candidates first (original
// order), AI candidates last (deduplicated against everything emitted).
class AiLastTranslation : public rime::Translation {
 public:
  explicit AiLastTranslation(rime::an<rime::Translation> translation) {
    while (!translation->exhausted()) {
      auto cand = translation->Peek();
      if (!cand)
        break;
      if (cand->type() == "ai_correction")
        ai_.push_back(cand);
      else
        normal_.push_back(cand);
      translation->Next();
    }
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
    if (cursor_ < normal_.size()) {
      current_ = normal_[cursor_++];
      set_exhausted(false);
      return;
    }
    while (ai_cursor_ < ai_.size()) {
      auto cand = ai_[ai_cursor_++];
      if (IsDuplicate(cand->text()))
        continue;
      emitted_.push_back(cand->text());
      current_ = cand;
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
  std::vector<rime::string> emitted_;
  rime::an<rime::Candidate> current_;
  bool advanced_ = false;
};

}  // namespace

AiCorrectionFilter::AiCorrectionFilter(const rime::Ticket& ticket)
    : Filter(ticket) {}

rime::an<rime::Translation> AiCorrectionFilter::Apply(
    rime::an<rime::Translation> translation,
    rime::CandidateList* candidates) {
  auto filtered = rime::New<AiLastTranslation>(translation);
  if (filtered->exhausted())
    return nullptr;
  return filtered;
}

}  // namespace weasel_ai
