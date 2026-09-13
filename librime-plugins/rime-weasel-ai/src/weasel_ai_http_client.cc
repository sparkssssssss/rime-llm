#include "weasel_ai_http_client.h"

#include <windows.h>
#include <winhttp.h>

#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace weasel_ai {

namespace {

struct UrlParts {
  bool is_https = true;
  std::wstring host;
  INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
  std::wstring path;
};

bool SplitUrl(const std::string& url, UrlParts* parts) {
  std::string rest;
  if (url.rfind("https://", 0) == 0) {
    parts->is_https = true;
    rest = url.substr(8);
  } else if (url.rfind("http://", 0) == 0) {
    parts->is_https = false;
    rest = url.substr(7);
  } else {
    return false;
  }
  size_t slash = rest.find('/');
  std::string host_port = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string path = slash == std::string::npos ? "/" : rest.substr(slash);

  std::string port_str;
  size_t colon = host_port.find(':');
  if (colon != std::string::npos) {
    port_str = host_port.substr(colon + 1);
    host_port = host_port.substr(0, colon);
  }
  if (host_port.empty())
    return false;

  int cp = MultiByteToWideChar(CP_UTF8, 0, host_port.c_str(), -1, nullptr, 0);
  if (cp <= 1)
    return false;
  std::vector<wchar_t> wbuf(static_cast<size_t>(cp));
  MultiByteToWideChar(CP_UTF8, 0, host_port.c_str(), -1, wbuf.data(), cp);
  parts->host = wbuf.data();

  cp = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
  if (cp <= 0)
    return false;
  std::vector<wchar_t> pbuf(static_cast<size_t>(cp));
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, pbuf.data(), cp);
  parts->path = pbuf.data();

  if (!port_str.empty()) {
    unsigned long value = 0;
    try {
      value = std::stoul(port_str);
    } catch (...) {
      return false;
    }
    if (value == 0 || value > 65535)
      return false;
    parts->port = static_cast<INTERNET_PORT>(value);
  } else {
    parts->port = parts->is_https ? INTERNET_DEFAULT_HTTPS_PORT
                                  : INTERNET_DEFAULT_HTTP_PORT;
  }
  return true;
}

std::string NarrowString(const std::wstring& wide) {
  if (wide.empty())
    return std::string();
  int cp = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                               static_cast<int>(wide.size()), nullptr, 0,
                               nullptr, nullptr);
  if (cp <= 0)
    return std::string();
  std::string out(static_cast<size_t>(cp), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      out.data(), cp, nullptr, nullptr);
  return out;
}

}  // namespace

HttpRequestResult HttpPostJson(const std::string& url,
                               const std::string& api_key,
                               const std::string& json_body,
                               int timeout_ms,
                               size_t max_response_bytes) {
  HttpRequestResult result;
  UrlParts parts;
  if (!SplitUrl(url, &parts)) {
    result.error = "invalid_url";
    return result;
  }

  HINTERNET session = WinHttpOpen(L"WeaselAI/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    result.error = "winhttp_open_failed";
    return result;
  }
  const int timeout = timeout_ms > 0 ? timeout_ms : 800;
  WinHttpSetOption(session, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
  WinHttpSetOption(session, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
  WinHttpSetOption(session, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

  HINTERNET connect = WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
  if (!connect) {
    result.error = "winhttp_connect_failed";
    WinHttpCloseHandle(session);
    return result;
  }

  const DWORD flags = parts.is_https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(
      connect, L"POST", parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!request) {
    result.error = "winhttp_open_request_failed";
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
  }

  std::wstring headers =
      L"Content-Type: application/json\r\n"
      L"Accept: application/json\r\n";
  if (!api_key.empty()) {
    headers += L"Authorization: Bearer " + NarrowString(api_key) + L"\r\n";
  }

  BOOL sent = WinHttpSendRequest(request, headers.c_str(),
                                 static_cast<DWORD>(headers.size()),
                                 const_cast<char*>(json_body.data()),
                                 static_cast<DWORD>(json_body.size()),
                                 static_cast<DWORD>(json_body.size()), 0);
  if (!sent) {
    result.error = "send_request_failed";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
  }

  if (!WinHttpReceiveResponse(request, nullptr)) {
    result.error = "receive_response_failed";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
  }

  DWORD status = 0;
  DWORD size = sizeof(status);
  if (WinHttpQueryHeaders(request,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                          WINHTTP_NO_HEADER_INDEX)) {
    result.status_code = static_cast<int>(status);
  }

  // Read the body; hard-stop at max_response_bytes.
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
      break;
    if (result.body.size() + available > max_response_bytes) {
      result.error = "response_too_large";
      break;
    }
    std::vector<char> buffer(available);
    DWORD read = 0;
    if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0)
      break;
    result.body.append(buffer.data(), read);
  }

  result.ok = result.error.empty() && result.status_code >= 200 &&
              result.status_code < 300;

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return result;
}

}  // namespace weasel_ai
