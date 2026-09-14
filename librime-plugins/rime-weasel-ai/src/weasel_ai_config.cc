#include "weasel_ai_config.h"

namespace weasel_ai {

namespace {

std::string TrimUrl(const std::string& url) {
  size_t begin = url.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos)
    return std::string();
  size_t end = url.find_last_not_of(" \t\r\n");
  std::string trimmed = url.substr(begin, end - begin + 1);
  while (!trimmed.empty() && trimmed.back() == '/')
    trimmed.pop_back();
  return trimmed;
}

}  // namespace

bool AiCorrectionConfig::Load(rime::Config* config,
                              const std::string& name_space) {
  if (!config)
    return false;
  const std::string prefix = name_space.empty() ? "ai_correction" : name_space;

  config->GetBool(prefix + "/enabled", &enabled);
  config->GetString(prefix + "/trigger_key", &trigger_key);
  if (auto list = config->GetList(prefix + "/trigger_keys")) {
    for (size_t i = 0; i < list->size(); ++i) {
      if (auto v = rime::As<rime::ConfigValue>(list->GetAt(i)))
        trigger_keys.push_back(v->str());
    }
  }
  config->GetString(prefix + "/base_url", &base_url);
  config->GetString(prefix + "/api_path", &api_path);
  config->GetString(prefix + "/model", &model);
  config->GetString(prefix + "/api_key", &api_key);
  config->GetString(prefix + "/api_key_env", &api_key_env);
  config->GetInt(prefix + "/timeout_ms", &timeout_ms);
  config->GetInt(prefix + "/max_candidates", &max_candidates);
  config->GetInt(prefix + "/max_result_bytes", &max_result_bytes);
  config->GetInt(prefix + "/min_input_length", &min_input_length);
  config->GetInt(prefix + "/max_input_length", &max_input_length);
  config->GetInt(prefix + "/context_window", &context_window);
  config->GetString(prefix + "/comment", &comment);
  config->GetString(prefix + "/candidate_position", &candidate_position);
  config->GetInt(prefix + "/page_size", &page_size);
  config->GetString(prefix + "/system_prompt", &system_prompt);
  config->GetString(prefix + "/prompt_file", &prompt_file);
  config->GetString(prefix + "/user_template", &user_template);
  config->GetDouble(prefix + "/temperature", &temperature);
  config->GetInt(prefix + "/max_tokens", &max_tokens);
  config->GetBool(prefix + "/deduplicate", &deduplicate);
  config->GetString(prefix + "/reasoning_effort", &reasoning_effort);
  config->GetString(prefix + "/extra_params", &extra_params);

  if (timeout_ms < 100)
    timeout_ms = 100;
  if (timeout_ms > 10000)
    timeout_ms = 10000;
  if (max_candidates < 1)
    max_candidates = 1;
  if (max_candidates > 5)
    max_candidates = 5;
  if (max_result_bytes < 32)
    max_result_bytes = 32;
  if (max_result_bytes > 4096)
    max_result_bytes = 4096;
  if (min_input_length < 0)
    min_input_length = 0;
  if (max_input_length < min_input_length)
    max_input_length = min_input_length;
  if (context_window < 0)
    context_window = 0;
  if (temperature < 0.0)
    temperature = 0.0;
  if (temperature > 2.0)
    temperature = 2.0;
  if (max_tokens < 16)
    max_tokens = 16;
  if (max_tokens > 2048)
    max_tokens = 2048;
  if (page_size < 1)
    page_size = 1;
  if (page_size > 9)
    page_size = 9;

  base_url = TrimUrl(base_url);
  if (api_path.empty() || api_path.front() != '/')
    api_path = "/" + api_path;

  return enabled && !base_url.empty() && !model.empty();
}

std::string AiCorrectionConfig::endpoint_url() const {
  return base_url + api_path;
}

}  // namespace weasel_ai
