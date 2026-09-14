// Test stubs: config endpoint and a fake HTTP transport.
#include "../src/weasel_ai_http_client.h"
#include "../src/weasel_ai_config.h"
#include <string>
namespace weasel_ai {
std::string AiCorrectionConfig::endpoint_url() const { return base_url + api_path; }
HttpRequestResult HttpPostJson(const std::string&, const std::string&,
                               const std::string&, int, size_t) {
  return HttpRequestResult{};
}
bool AiCorrectionConfig::Load(rime::Config*, const std::string&) { return false; }
}
