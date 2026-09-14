#include "weasel_ai_correction_service.h"

#include <cstdlib>

#include "weasel_ai_http_client.h"
#include "weasel_ai_json.h"

namespace weasel_ai {

namespace {

constexpr size_t kMaxResponseBytes = 512 * 1024;

// UTF-8 text must not contain C0/C1 control characters (except nothing at
// all for candidate text) and must decode as valid UTF-8.
bool IsValidUtf8NoControl(const std::string& text) {
  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x20 || c == 0x7F)
      return false;  // C0 control or DEL
    if (c == 0xC2 || c == 0xC3) {
      // C1 controls are encoded as C2 80..C2 9F; reject that range.
      if (i + 1 >= n)
        return false;
      unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
      if (c == 0xC2 && c2 >= 0x80 && c2 <= 0x9F)
        return false;
    }
    size_t len = 0;
    unsigned int cp = 0;
    if (c < 0x80) {
      ++i;
      continue;
    } else if ((c & 0xE0) == 0xC0) {
      len = 2;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + len > n)
      return false;
    for (size_t k = 1; k < len; ++k) {
      unsigned char cc = static_cast<unsigned char>(text[i + k]);
      if ((cc & 0xC0) != 0x80)
        return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    // overlong / range checks
    if (len == 2 && cp < 0x80)
      return false;
    if (len == 3 && cp < 0x800)
      return false;
    if (len == 4 && cp < 0x10000)
      return false;
    if (cp > 0x10FFFF)
      return false;
    if (cp >= 0xD800 && cp <= 0xDFFF)
      return false;
    i += len;
  }
  return true;
}

std::string JoinCandidatesForPrompt(const std::vector<std::string>& candidates,
                                    size_t max_count) {
  std::string joined;
  size_t count = 0;
  for (const auto& cand : candidates) {
    if (count >= max_count)
      break;
    if (!cand.empty() && IsValidUtf8NoControl(cand)) {
      if (!joined.empty())
        joined += "；";
      joined += cand;
      ++count;
    }
  }
  return joined;
}

std::string ClipContext(const std::string& context, int max_chars) {
  if (max_chars <= 0 || context.empty())
    return std::string();
  // clip on UTF-8 boundary, keep the tail
  size_t bytes = context.size();
  size_t chars = 0;
  size_t cut = 0;
  for (size_t i = bytes; i > 0;) {
    // find start of last utf8 char
    size_t pos = i - 1;
    while (pos > 0 &&
           (static_cast<unsigned char>(context[pos]) & 0xC0) == 0x80)
      --pos;
    ++chars;
    if (static_cast<int>(chars) > max_chars) {
      cut = pos;
      break;
    }
    if (pos == 0) {
      cut = 0;
      break;
    }
    i = pos;
  }
  return cut == 0 && static_cast<int>(chars) <= max_chars
             ? context
             : context.substr(cut);
}

// Literal api_key wins; otherwise read the key from the configured
// environment variable so deployments can keep secrets out of YAML.
std::string ResolveApiKey(const AiCorrectionConfig& config) {
  if (!config.api_key.empty())
    return config.api_key;
  if (!config.api_key_env.empty()) {
    const char* value = std::getenv(config.api_key_env.c_str());
    if (value && *value)
      return std::string(value);
  }
  return std::string();
}

const char* kDefaultSystemPrompt =
    "你是中文输入法的整句校准助手。用户会给出一段无分隔的拼音串、"
    "输入法当前的候选（可能全部是错的）、以及少量上下文。"
    "你的任务是把拼音串逐音节转写为最可能的中文句子，"
    "【不要】从当前候选里直接挑选——当候选与拼音的逐字转写不一致时，"
    "以你自己的转写为准。特别注意区分同音字（如 把/芭/吧、校/蕉/叫、词/辞/次），"
    "并根据整句语义选择正确的汉字。"
    "例子："
    "输入 weishenmebajiaozhundehouxuancifangzainamekaohou，"
    "候选[为什么芭蕉准的候选次方在那么靠后]——"
    "正确输出是【为什么把校准的候选词放在那么靠后】"
    "（bǎ→把，jiào→校，cí→词，fàng→放，而不是芭蕉/准/次方/靠后）。"
    "只返回JSON，格式："
    "{\"candidates\":[{\"text\":\"校准后的句子\",\"score\":0.9}]}，"
    "不要输出任何解释、markdown或多余文字。";

}  // namespace

CorrectionService::CorrectionService(const AiCorrectionConfig& config)
    : config_(config) {}

std::string CorrectionService::BuildRequestBody(
    const CorrectionRequest& request) const {
  auto root = JsonValue::MakeObject();
  root->set("model", JsonValue::MakeString(config_.model));

  auto messages = JsonValue::MakeArray();

  auto sys = JsonValue::MakeObject();
  sys->set("role", JsonValue::MakeString("system"));
  sys->set("content", JsonValue::MakeString(
                          config_.system_prompt.empty() ? std::string(kDefaultSystemPrompt)
                                                        : config_.system_prompt));
  messages->push(sys);

  auto user = JsonValue::MakeObject();
  user->set("role", JsonValue::MakeString("user"));
  std::string user_content = "原始输入：" + request.input;
  std::string joined = JoinCandidatesForPrompt(request.candidates, 5);
  if (!joined.empty())
    user_content += "\n当前候选：" + joined;
  if (!request.committed_context.empty())
    user_content += "\n上下文：" + request.committed_context;
  user->set("content", JsonValue::MakeString(user_content));
  messages->push(user);

  root->set("messages", messages);
  root->set("temperature", JsonValue::MakeNumber(config_.temperature));
  root->set("max_tokens", JsonValue::MakeNumber(config_.max_tokens));
  root->set("stream", JsonValue::MakeBool(false));

  if (!config_.reasoning_effort.empty()) {
    root->set("reasoning_effort",
              JsonValue::MakeString(config_.reasoning_effort));
  }
  // extra_params is merged last so it can override any field above.
  if (!config_.extra_params.empty()) {
    std::string parse_error;
    JsonPtr extra = JsonParse(config_.extra_params, &parse_error);
    if (extra && extra->is_object()) {
      for (const auto& kv : extra->RawMembers()) {
        if (kv.second)
          root->set(kv.first, kv.second);
      }
    }
    // Invalid JSON is ignored on purpose: a bad extra_params must never
    // break the request (logged by the caller, not here, to keep this
    // translation unit free of logging dependencies).
  }

  return root->Dump();
}

CorrectionResponse CorrectionService::ParseResponseBody(
    const std::string& body,
    size_t max_candidates,
    size_t max_candidate_bytes) {
  CorrectionResponse response;
  std::string error;
  JsonPtr root = JsonParse(body, &error);
  if (!root) {
    response.error = "invalid_json: " + error;
    return response;
  }
  const JsonPtr& choices = root->get("choices");
  if (!choices->is_array() || choices->size() == 0) {
    response.error = "missing_choices";
    return response;
  }
  const JsonPtr& message = choices->at(0)->get("message");
  if (!message || !message->is_object()) {
    response.error = "missing_message";
    return response;
  }
  const JsonPtr& content = message->get("content");
  if (!content->is_string()) {
    response.error = "missing_content";
    return response;
  }
  const std::string& content_text = content->as_string();
  if (content_text.empty()) {
    // Reasoning models (DeepSeek-R1 style) put output in reasoning_content
    // and may return empty content when max_tokens is exhausted by thinking.
    const JsonPtr& reasoning = message->get("reasoning_content");
    if (reasoning->is_string() && !reasoning->as_string().empty()) {
      response.error =
          "empty_content_with_reasoning: increase ai_correction/max_tokens "
          "(e.g. 2048) or disable the model's thinking mode";
    } else {
      response.error = "empty_content";
    }
    return response;
  }
  // The model may wrap JSON in a markdown fence; locate the outermost object.
  size_t begin = content_text.find('{');
  size_t end = content_text.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    response.error = "content_not_json";
    return response;
  }
  JsonPtr payload = JsonParse(content_text.substr(begin, end - begin + 1), &error);
  if (!payload || !payload->is_object()) {
    response.error = "invalid_content_json: " + error;
    return response;
  }
  const JsonPtr& cands = payload->get("candidates");
  if (!cands->is_array()) {
    response.error = "missing_candidates";
    return response;
  }
  size_t count = cands->size() < max_candidates ? cands->size() : max_candidates;
  for (size_t i = 0; i < count; ++i) {
    const JsonPtr& item = cands->at(i);
    if (!item || !item->is_object())
      continue;
    const JsonPtr& text = item->get("text");
    if (!text->is_string())
      continue;
    const std::string& cand_text = text->as_string();
    if (!IsSafeCandidateText(cand_text, max_candidate_bytes))
      continue;
    AiCandidate cand;
    cand.text = cand_text;
    const JsonPtr& score = item->get("score");
    if (score->is_number())
      cand.score = score->as_number();
    response.candidates.push_back(std::move(cand));
  }
  if (response.candidates.empty()) {
    response.error = "no_valid_candidates";
    return response;
  }
  response.ok = true;
  return response;
}

CorrectionResponse CorrectionService::Correct(
    const CorrectionRequest& request) const {
  CorrectionResponse response;
  std::string body = BuildRequestBody(request);
  HttpRequestResult http = HttpPostJson(config_.endpoint_url(), ResolveApiKey(config_),
                                        body, config_.timeout_ms,
                                        kMaxResponseBytes);
  if (!http.ok) {
    response.error = http.error.empty() ? ("http_" + std::to_string(http.status_code))
                                        : http.error;
    return response;
  }
  return ParseResponseBody(http.body,
                           static_cast<size_t>(config_.max_candidates),
                           static_cast<size_t>(config_.max_result_bytes));
}

bool IsSafeCandidateText(const std::string& text, size_t max_bytes) {
  if (text.empty() || text.size() > max_bytes)
    return false;
  return IsValidUtf8NoControl(text);
}

}  // namespace weasel_ai
