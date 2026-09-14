#include "weasel_ai_processor.h"


#include <filesystem>
#include <fstream>

#include <rime/commit_history.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/menu.h>
#include <rime/schema.h>
#include <rime/service.h>
#include <rime/segmentation.h>

#include "weasel_ai_correction_service.h"
#include "weasel_ai_json.h"
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
  // Borrowed pointer: the registry owns the store (see store_registry.h).
  store_ = FindOrCreateStore(ticket.engine);
  // A new component set means a (possibly reused) engine; drop any result
  // left over from a previous session at the same address.
  if (store_)
    store_->Invalidate();
  LoadConfig();
}

void AiCorrectionProcessor::LoadConfig() {
  if (!engine_ || !engine_->schema())
    return;
  trigger_loaded_ = false;
  // Config node is fixed to "ai_correction" (matches docs & example yaml).
  // NOTE: do NOT pass name_space_ here - for a prescription without an
  // explicit "@alias" it equals the class name ("ai_correction_processor"),
  // which is NOT where the config lives.
  LOG(INFO) << "[weasel_ai] LoadConfig begin; schema_id="
            << (engine_->schema() ? engine_->schema()->schema_id() : "(null)");
  if (!config_.Load(engine_->schema()->config(), "ai_correction")) {
    // Fall back to global default config (default.yaml / default.custom.yaml)
    // so users can enable the feature for ALL schemas in one place.
    LOG(INFO) << "[weasel_ai] schema config did not carry ai_correction; "
              << "falling back to deployed default config";
    rime::the<rime::Config> global(rime::Config::Require("config")->Create("default"));
    if (!global) {
      LOG(ERROR) << "[weasel_ai] fallback Config::Create(\"default\") returned NULL";
      return;
    }
    {
      bool en = false;
      std::string m, u;
      bool got_en = global->GetBool("ai_correction/enabled", &en);
      bool got_m = global->GetString("ai_correction/model", &m);
      bool got_u = global->GetString("ai_correction/base_url", &u);
      LOG(INFO) << "[weasel_ai] default-config probe: GetBool(enabled)=" << got_en
                << " value=" << en << " GetBool(model)=" << got_m
                << " GetString(base_url)=" << got_u;
    }
    if (!config_.Load(global.get(), "ai_correction")) {
      LOG(ERROR) << "[weasel_ai] config disabled or incomplete "
                 << "(enabled/model/base_url) in both schema and default config";
      return;  // disabled or misconfigured: processor stays inert
    } else {
      LOG(INFO) << "[weasel_ai] config loaded from default: model="
                << config_.model << " trigger=" << config_.trigger_key
                << " timeout_ms=" << config_.timeout_ms
                << " max_tokens=" << config_.max_tokens
                << " reasoning_effort=" << config_.reasoning_effort;
    }
  }
  LoadSystemPromptFile();
  if (!config_.extra_params.empty()) {
    std::string json_error;
    if (!JsonParse(config_.extra_params, &json_error)) {
      LOG(WARNING) << "[weasel_ai] ai_correction/extra_params is not valid "
                      "JSON and will be ignored: "
                   << json_error;
    }
  }
  triggers_.clear();
  std::vector<std::string> reps;
  if (!config_.trigger_key.empty())
    reps.push_back(config_.trigger_key);
  for (const auto& extra : config_.trigger_keys) {
    if (!extra.empty())
      reps.push_back(extra);
  }
  for (const auto& rep : reps) {
    rime::KeyEvent key;
    if (!key.Parse(rep) || key.keycode() == 0)
      continue;
    // ignore duplicates
    bool dup = false;
    for (const auto& t : triggers_)
      if (t.keycode() == key.keycode() && t.modifier() == key.modifier())
        dup = true;
    if (!dup)
      triggers_.push_back(key);
  }
  trigger_loaded_ = !triggers_.empty();
  for (const auto& t : triggers_) {
    LOG(INFO) << "[weasel_ai] trigger parsed: keycode=0x" << std::hex
              << t.keycode() << std::dec << " modifier=0x" << t.modifier();
  }
}

// If ai_correction/prompt_file is set, load it and let it override the inline
// prompt. Relative paths are resolved against the Rime user directory so the
// file can simply sit next to default.custom.yaml.
void AiCorrectionProcessor::LoadSystemPromptFile() {
  if (config_.prompt_file.empty())
    return;
  namespace fs = std::filesystem;
  fs::path path = config_.prompt_file;
  std::error_code ec;
  if (path.is_relative()) {
    const fs::path user_dir = rime::Service::instance().deployer().user_data_dir;
    if (!user_dir.empty())
      path = user_dir / path;
  }
  if (!fs::exists(path, ec)) {
    LOG(WARNING) << "[weasel_ai] ai_correction/prompt_file not found: "
                 << path.string() << " (using inline/default prompt)";
    return;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    LOG(WARNING) << "[weasel_ai] ai_correction/prompt_file cannot be read: "
                 << path.string();
    return;
  }
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  // strip UTF-8 BOM and trim trailing whitespace
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF)
    text.erase(0, 3);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' ||
                           text.back() == ' ' || text.back() == '\t'))
    text.pop_back();
  if (text.empty()) {
    LOG(WARNING) << "[weasel_ai] ai_correction/prompt_file is empty: "
                 << path.string();
    return;
  }
  if (text.size() > 8192) {
    LOG(WARNING) << "[weasel_ai] ai_correction/prompt_file too large ("
                 << text.size() << " bytes); truncated to 8192";
    text.resize(8192);
  }
  config_.system_prompt = std::move(text);
  LOG(INFO) << "[weasel_ai] prompt loaded from file: " << path.string()
            << " (" << config_.system_prompt.size() << " bytes)";
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
  const size_t pool =
      config_.mode == "rerank"
          ? static_cast<size_t>(config_.rerank_pool < 2 ? 2 : config_.rerank_pool)
          : static_cast<size_t>(5);
  if (seg.menu) {
    for (size_t i = 0; i < pool; ++i) {
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
    LOG(INFO) << "[weasel_ai] correction ok, mode=" << config_.mode
              << " picked_index=" << response.picked_index
              << " candidates=" << response.candidates.size();
  } else if (response.error == "rerank_no_change") {
    // Selection mode: the model kept the default first candidate, so there is
    // nothing to add - not an error.
    LOG(INFO) << "[weasel_ai] rerank: model agrees with the first candidate";
  } else {
    LOG(ERROR) << "[weasel_ai] correction failed: " << response.error;
  }
  AiResult result;
  result.input = segment_input;
  result.seg_start = seg.start;
  result.seg_end = seg.end;
  result.has_range = true;
  result.comment = config_.comment;
  if (response.ok) {
    result.candidates = std::move(response.candidates);
  }
  // Commit only if no newer request started meanwhile (same thread here,
  // but the guard keeps semantics correct for the future async manager).
  if (!store_->Commit(token, std::move(result)))
    return;
  LOG(INFO) << "[weasel_ai] result committed; refreshing non-confirmed composition";

  // Re-run translation for the unconfirmed tail so the menu picks up the
  // stored AI result. RefreshNonConfirmedComposition re-opens unconfirmed
  // segments and fires the update notifier; Weasel then repaints through its
  // normal ProcessKeyEvent path.
  bool refreshed = ctx->RefreshNonConfirmedComposition();
  LOG(INFO) << "[weasel_ai] RefreshNonConfirmedComposition -> " << refreshed;

  // Move the selection to the AI candidate so Weasel shows the page that
  // contains it. The filter records the exact index; fall back to a bounded
  // scan if it did not run (e.g. another filter short-circuited).
  if (refreshed) {
    size_t target = store_->ai_index();
    if (target == AiResultStore::kNoIndex) {
      auto& seg2 = ctx->composition().back();
      if (seg2.menu) {
        for (size_t i = 0; i < 20; ++i) {
          auto cand = seg2.menu->GetCandidateAt(i);
          if (!cand)
            break;
          if (cand->type() == "ai_correction") {
            target = i;
            break;
          }
        }
      }
    }
    if (target != AiResultStore::kNoIndex && ctx->Highlight(target)) {
      LOG(INFO) << "[weasel_ai] highlighted AI candidate at index " << target;
    }
  }
}

rime::ProcessResult AiCorrectionProcessor::ProcessKeyEvent(
    const rime::KeyEvent& key_event) {
  if (!config_.enabled || !trigger_loaded_ || !engine_)
    return rime::kNoop;
  // Match on keycode + "all trigger modifiers held". State modifiers that
  // ride along (NumLock = Mod2, etc.) must not break the match, so compare
  // with containment instead of strict equality.
  const int event_modifiers = key_event.modifier() & 0xff;
  const rime::KeyEvent* hit = nullptr;
  for (const auto& t : triggers_) {
    const int trigger_modifiers = t.modifier() & 0xff;
    if (key_event.keycode() == t.keycode() &&
        (event_modifiers & trigger_modifiers) == trigger_modifiers &&
        (event_modifiers & ~(trigger_modifiers | kLockMask)) == 0) {
      hit = &t;
      break;
    }
  }
  if (!hit) {
    // Log only near-misses on the trigger keycode to keep the log lean.
    for (const auto& t : triggers_) {
      if (key_event.keycode() == t.keycode()) {
        LOG(INFO) << "[weasel_ai] key near-miss: keycode=0x" << std::hex
                  << key_event.keycode() << std::dec << " modifier=0x"
                  << event_modifiers << " (want 0x"
                  << (t.modifier() & 0xff) << ")";
      }
    }
    return rime::kNoop;
  }
  LOG(INFO) << "[weasel_ai] trigger key down matched";
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
