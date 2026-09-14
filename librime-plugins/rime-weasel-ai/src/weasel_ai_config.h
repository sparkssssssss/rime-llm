// Weasel AI correction - configuration.
//
// Configuration is read from the schema config (schema_id.yaml merged with
// default.yaml / default.custom.yaml through the normal Rime config chain).
// All keys live under the `ai_correction` node. Missing config means the
// feature is OFF: the plugin must never phone home uninvited.
//
// Example (in default.custom.yaml or <schema>.custom.yaml):
//
//   patch:
//     ai_correction/enabled: true
//     ai_correction/trigger_key: Control+grave
//     ai_correction/base_url: https://api.openai.com
//     ai_correction/api_path: /v1/chat/completions
//     ai_correction/model: gpt-4o-mini
//     ai_correction/api_key: sk-xxx
//     ai_correction/timeout_ms: 800
//
// api_key may be a literal secret, or leave it empty and set api_key_env to
// the name of an environment variable that holds the key (preferred: keeps
// secrets out of the YAML and out of logs).

#ifndef WEASEL_AI_CONFIG_H_
#define WEASEL_AI_CONFIG_H_

#include <string>
#include <vector>

#include <rime/common.h>
#include <rime/config.h>

namespace weasel_ai {

struct AiCorrectionConfig {
  bool Load(rime::Config* config, const std::string& name_space);

  bool enabled = false;
  std::string trigger_key = "Control+grave";  // repr parsed at use time
  std::vector<std::string> trigger_keys;      // extra trigger keys (optional)
  std::string base_url = "https://api.openai.com";
  std::string api_path = "/v1/chat/completions";
  std::string model;
  std::string api_key;      // literal secret (MVP)
  std::string api_key_env;  // env var name holding the key (used when api_key empty)
  int timeout_ms = 800;
  int max_candidates = 1;
  int max_result_bytes = 512;  // per-candidate byte cap for AI output
  int min_input_length = 6;
  int max_input_length = 64;
  int context_window = 100;     // max chars of commit history sent, 0 = off
  std::string comment = "AI校准";
  // "last" (default): append after all normal candidates
  // "page1_end": insert at the end of the FIRST page (index page_size-1)
  std::string candidate_position = "last";
  int page_size = 5;
  std::string system_prompt;
  double temperature = 0.2;
  int max_tokens = 256;
  // Optional request knobs. reasoning_effort="none" disables thinking on
  // reasoning models (huge latency win). extra_params is a raw JSON object
  // merged into the request body last, for gateway-specific options.
  // When true, an AI candidate whose text duplicates an existing candidate is
  // suppressed. Default false: always show it, so the user gets feedback that
  // the AI ran even when it agrees with the built-in candidates.
  bool deduplicate = false;
  std::string reasoning_effort;
  std::string extra_params;

  std::string endpoint_url() const;
};

}  // namespace weasel_ai

#endif  // WEASEL_AI_CONFIG_H_
