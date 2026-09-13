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
// MVP supports a literal api_key for simplicity; a key reference (env var /
// credential store) is a planned follow-up and `api_key_env` is already read
// so the format can be finalized without breaking users.

#ifndef WEASEL_AI_CONFIG_H_
#define WEASEL_AI_CONFIG_H_

#include <string>

#include <rime/common.h>
#include <rime/config.h>

namespace weasel_ai {

struct AiCorrectionConfig {
  bool Load(rime::Config* config, const std::string& name_space);

  bool enabled = false;
  std::string trigger_key = "Control+grave";  // repr parsed at use time
  std::string base_url = "https://api.openai.com";
  std::string api_path = "/v1/chat/completions";
  std::string model;
  std::string api_key;      // literal secret (MVP)
  std::string api_key_env;  // future: read key from environment variable
  int timeout_ms = 800;
  int max_candidates = 1;
  int min_input_length = 6;
  int max_input_length = 64;
  int context_window = 100;     // max chars of commit history sent, 0 = off
  std::string comment = "AI校准";
  std::string system_prompt;
  double temperature = 0.2;
  int max_tokens = 256;

  std::string endpoint_url() const;
};

}  // namespace weasel_ai

#endif  // WEASEL_AI_CONFIG_H_
