// Weasel AI correction - trigger processor.
//
// Listens for the configured trigger key while a composition is active.
// On trigger it:
//   1. grabs the current segment input (the unconfirmed composition),
//   2. builds a CorrectionRequest from input + existing candidates +
//      commit-history context,
//   3. performs a BLOCKING OpenAI-compatible request (MVP synchronous
//      design; bounded by timeout_ms),
//   4. stores the result tagged with the current input,
//   5. refreshes the unconfirmed composition so the engine re-runs
//      translation and the new AI candidate appears at the end of the menu.
//
// The key is consumed on trigger regardless of request success, so the AI
// shortcut never types a character into the application.

#ifndef WEASEL_AI_PROCESSOR_H_
#define WEASEL_AI_PROCESSOR_H_

#include <rime/key_event.h>

#include <vector>
#include <rime/processor.h>

#include "weasel_ai_config.h"
#include "weasel_ai_result_store.h"

namespace weasel_ai {

class Config;

class AiCorrectionProcessor : public rime::Processor {
 public:
  explicit AiCorrectionProcessor(const rime::Ticket& ticket);

  rime::ProcessResult ProcessKeyEvent(const rime::KeyEvent& key_event) override;

 private:
  void LoadConfig();
  void LoadSystemPromptFile();
  rime::string CurrentSegmentInput() const;
  void TriggerCorrection(const rime::string& segment_input);

  AiCorrectionConfig config_;
  AiResultStore* store_ = nullptr;  // borrowed from the registry
  std::vector<rime::KeyEvent> triggers_;
  bool trigger_loaded_ = false;
};

}  // namespace weasel_ai

#endif  // WEASEL_AI_PROCESSOR_H_
