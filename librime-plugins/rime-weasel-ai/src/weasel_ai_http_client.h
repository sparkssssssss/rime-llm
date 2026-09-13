// Weasel AI correction - blocking HTTP POST client for Windows (WinHTTP).
//
// The client is intentionally minimal and synchronous with a hard timeout:
// it is ONLY called from the AI correction Processor in response to an
// explicit user trigger, never from the ordinary typing path. The async
// request manager (next phase) will reuse this client on a worker thread.
//
// HTTPS: certificates are validated by default system settings. The API key
// is sent as `Authorization: Bearer <key>` when non-empty, and is never
// logged.

#ifndef WEASEL_AI_HTTP_CLIENT_H_
#define WEASEL_AI_HTTP_CLIENT_H_

#include <string>

namespace weasel_ai {

struct HttpRequestResult {
  bool ok = false;          // transport + HTTP status both fine (2xx)
  int status_code = 0;      // HTTP status (0 = transport error)
  std::string error;        // machine-readable transport error
  std::string body;         // response body (possibly empty)
};

// Blocking POST with JSON body. timeout_ms bounds the whole operation.
HttpRequestResult HttpPostJson(const std::string& url,
                               const std::string& api_key,
                               const std::string& json_body,
                               int timeout_ms,
                               size_t max_response_bytes);

}  // namespace weasel_ai

#endif  // WEASEL_AI_HTTP_CLIENT_H_
