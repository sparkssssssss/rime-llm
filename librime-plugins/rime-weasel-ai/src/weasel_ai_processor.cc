#include "weasel_ai_processor.h"


#include <rime/commit_history.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/menu.h>
#include <rime/schema.h>
#include <rime/segmentation.h>

#include "weasel_ai_correction_service.h"
#include "weasel_ai_store_registry.h"

namespace weasel_ai {

namespace {

// Builds the context snippet from recent commit history, keeping the tail.
// Only linguistic commit types are included; key events ("thru") and
// internal markers are skipped to avoid leaking raw keycodes into prompts.
std::string ClipContextForRequest(const rime::CommitHistory& history,
                                  int max_chars) {
  std::string text;
  for (auto it = history.rbegin(); it != history.rend(); ++it) {
    if (it->type == "thru" || it->type == "key" || it->type.empty())
      continue;
    if (it->text.empty())
      continue;
    text = it->text + text;
    if (static_cast<int>(text.size()) >= max_chars * 4)
      break;
  }
  if (text.empty())
    return text;
  // Clip to max_chars UTF-8 characters, keeping the tail.
  int chars = 0;
  size_t cut = text.size();
  for (size_t i = text.size(); i > 0;) {
    size_t pos = i - 1;
    while (pos > 0 &&
           (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
      --pos;
    ++chars;
    if (chars > max_chars) {
      cut = pos;
      break;
    }
    if (pos == 0)
      break;
    i = pos;
  }
  return text.substr(cut);
}

}  // namespace

AiCorrectionProcessor::AiCorrectionProcessor(const rime::Ticket& ticket)
    : Processor(ticket) {
  store_.reset(FindOrCreateStore(ticket.engine));
  LoadConfig();
}

void AiCorrectionProcessor::LoadConfig() {
  if (!engine_ || !engine_->schema())
    return;
  trigger_loaded_ = false;
  // Schema-level config first (ai_correction under <schema>.schema.yaml).
  if (!config_.Load(engine_->schema()->config(), name_space_)) {
    // Fall back to global default config (default.yaml / default.custom.yaml)
    // so users can enable the feature for ALL schemas in one place.
    rime::the<rime::Config> global(rime::Config::Require("config")->Create("default"));
    if (!config_.Load(global.get(), name_space_)) {
      LOG(ERROR) << "[weasel_ai] config disabled or incomplete "
                 << "(enabled/model/base_url) in both schema and default config";
      return;  // disabled or misconfigured: processor stays inert
    } else {
      LOG(INFO) << "[weasel_ai] config loaded from default: model="
                << config_.model << " trigger=" << config_.trigger_key
                << " timeout_ms=" << config_.timeout_ms;
    }
  }
  trigger_.Parse(config_.trigger_key);
  trigger_loaded_ = !config_.trigger_key.empty() && trigger_.keycode() != 0;
  LOG(INFO) << "[weasel_ai] trigger parsed: keycode=0x" << std::hex
            << trigger_.keycode() << std::dec << " modifier=0x"
            << trigger_.modifier() << " loaded=" << trigger_loaded_;
}

rime::string AiCorrectionProcessor::CurrentSegmentInput() const {
  if (!engine_)
    return rime::string();
  auto* ctx = engine_->context();
  if (!ctx || !ctx->IsComposing())
    return rime::string();
  auto& comp = ctx->composition();
  if (comp.empty())
    return rime::string();
  // The last segment is the one being edited (unconfirmed tail).
  auto& seg = comp.back();
  if (seg.status >= rime::Segment::kSelected)
    return rime::string();  // a confirmed selection is pending commit
  const rime::string& input = ctx->input();
  if (seg.start >= input.size() || seg.end > input.size() ||
      seg.end <= seg.start)
    return rime::string();
  return input.substr(seg.start, seg.end - seg.start);
}

void AiCorrectionProcessor::TriggerCorrection(
    const rime::string& segment_input) {
  auto* ctx = engine_->context();
  auto& comp = ctx->composition();
  auto& seg = comp.back();

  CorrectionRequest request;
  request.input = segment_input;
  // Existing candidates from the current menu (dedup happens in the filter).
  if (seg.menu) {
    for (size_t i = 0; i < 5; ++i) {
      auto cand = seg.menu->GetCandidateAt(i);
      if (!cand)
        break;
      request.candidates.push_back(cand->text());
    }
  }
  // Committed context window (configurable; 0 disables).
  if (config_.context_window > 0) {
    request.committed_context =
        ClipContextForRequest(ctx->commit_history(), config_.context_window);
  }

  int token = store_->BeginRequest();
  CorrectionService service(config_);
  CorrectionResponse response = service.Correct(request);
  if (response.ok) {
    LOG(INFO) << "[weasel_ai] correction ok, candidates="
              << response.candidates.size();
  } else {
    LOG(ERROR) << "[weasel_ai] correction failed: " << response.error;
  }
  AiResult result;
  result.input = segment_input;
  if (response.ok) {
    result.candidates = std::move(response.candidates);
  }
  // Commit only if no newer request started meanwhile (same thread here,
  // but the guard keeps semantics correct for the future async manager).
  if (!store_->Commit(token, std::move(result)))
    return;

  // Re-run translation for the unconfirmed tail so the menu picks up the
  // stored AI result. RefreshNonConfirmedComposition re-opens unconfirmed
  // segments and fires the update notifier; Weasel then repaints through its
  // normal ProcessKeyEvent path.
  ctx->RefreshNonConfirmedComposition();
}

rime::ProcessResult AiCorrectionProcessor::ProcessKeyEvent(
    const rime::KeyEvent& key_event) {
  if (!config_.enabled || !trigger_loaded_ || !engine_)
    return rime::kNoop;
  // Match on keycode + "all trigger modifiers held". State modifiers that
  // ride along (NumLock = Mod2, etc.) must not break the match, so compare
  // with containment instead of strict equality.
  const int trigger_modifiers = trigger_.modifier() & 0xff;
  const int event_modifiers = key_event.modifier() & 0xff;
  LOG(INFO) << "[weasel_ai] key: keycode=0x" << std::hex
            << key_event.keycode() << std::dec << " modifier=0x"
            << event_modifiers << " (trigger 0x" << std::hex
            << trigger_.keycode() << std::dec << "/0x" << std::hex
            << trigger_modifiers << std::dec << ")";
  if (key_event.keycode() != trigger_.keycode() ||
      (event_modifiers & trigger_modifiers) != trigger_modifiers ||
      event_modifiers & ~(trigger_modifiers | kLockMask))
    return rime::kNoop;
  if (key_event.release())
    return rime::kAccepted;  // swallow release of the trigger too

  rime::string segment_input = CurrentSegmentInput();
  if (segment_input.empty() ||
      static_cast<int>(segment_input.size()) < config_.min_input_length ||
      static_cast<int>(segment_input.size()) > config_.max_input_length)
    return rime::kAccepted;

  LOG(INFO) << "[weasel_ai] TRIGGERED, input=\"" << segment_input << "\"";
  TriggerCorrection(segment_input);
  return rime::kAccepted;
}

}  // namespace weasel_ai
