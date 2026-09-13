// Unit tests for the weasel_ai JSON parser and correction service parsing.
// Standalone: compiled and run directly with g++ on the dev box, no librime
// dependency (service header includes rime common only via config header).

#include <cassert>
#include <iostream>

#include "../src/weasel_ai_json.h"
#include "../src/weasel_ai_correction_service.h"

using namespace weasel_ai;

static void TestParseBasic() {
  std::string error;
  auto v = JsonParse(R"({"a":1,"b":[true,false,null,"x\ny"],"c":"é"})", &error);
  assert(v);
  assert(error.empty());
  assert(v->get("a")->as_number() == 1.0);
  assert(v->get("b")->at(0)->as_bool() == true);
  assert(v->get("b")->at(2)->is_null());
  assert(v->get("b")->at(3)->as_string() == "x\ny");
  assert(v->get("c")->as_string() == "\xC3\xA9");
}

static void TestParseUnicodeEscape() {
  std::string error;
  // "中" = U+4E2D; surrogate pair for U+1F600
  auto v = JsonParse(R"({"zh":"\u4e2d","emoji":"\ud83d\ude00"})", &error);
  assert(v && error.empty());
  assert(v->get("zh")->as_string() == "\xE4\xB8\xAD");
  assert(v->get("emoji")->as_string() == "\xF0\x9F\x98\x80");
}

static void TestParseErrors() {
  std::string error;
  assert(!JsonParse("{", &error));
  assert(!JsonParse("{\"a\":}", &error));
  assert(!JsonParse("nul", &error));
  assert(!JsonParse("\"\\x\"", &error));
  assert(!JsonParse("{} tail", &error));
  assert(!JsonParse("[1,]", &error));
}

static void TestDump() {
  auto obj = JsonValue::MakeObject();
  obj->set("model", JsonValue::MakeString("gpt-test"));
  obj->set("stream", JsonValue::MakeBool(false));
  obj->set("max_tokens", JsonValue::MakeNumber(256));
  auto arr = JsonValue::MakeArray();
  auto msg = JsonValue::MakeObject();
  msg->set("role", JsonValue::MakeString("user"));
  msg->set("content", JsonValue::MakeString("引\"号\"与\n换行"));
  arr->push(msg);
  obj->set("messages", arr);
  std::string body = obj->Dump();
  std::string error;
  auto roundtrip = JsonParse(body, &error);
  assert(roundtrip && error.empty());
  assert(roundtrip->get("messages")->at(0)->get("content")->as_string() ==
         "引\"号\"与\n换行");
}

static void TestParseChatCompletion() {
  const char* body =
      "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":"
      "\"{\\\"candidates\\\":[{\\\"text\\\":\\\"我想去北京天安门。\\\","
      "\\\"score\\\":0.92}]}\",\"finish_reason\":\"stop\"}}]}";
  auto resp = CorrectionService::ParseResponseBody(body, 1);
  assert(resp.ok);
  assert(resp.candidates.size() == 1);
  assert(resp.candidates[0].text == "我想去北京天安门。");
  assert(resp.candidates[0].score > 0.9);
}

static void TestParseChatCompletionMarkdownFence() {
  const char* body =
      "{\"choices\":[{\"message\":{\"content\":\"```json\\n{\\\"candidates\\\":"
      "[{\\\"text\\\":\\\"结果\\\"}]}\\n```\"}}]}";
  auto resp = CorrectionService::ParseResponseBody(body, 3);
  assert(resp.ok);
  assert(resp.candidates.size() == 1);
  assert(resp.candidates[0].text == "结果");
}

static void TestRejectControlChars() {
  const char* body =
      "{\"choices\":[{\"message\":{\"content\":\"{\\\"candidates\\\":[{\\\"text\\\":"
      "\\\"bad\\u0001text\\\"}]}\"}}]}";
  auto resp = CorrectionService::ParseResponseBody(body, 3);
  // \u0001 decodes to a C0 control character - candidate must be rejected.
  assert(!resp.ok);
}

static void TestBadResponses() {
  auto r1 = CorrectionService::ParseResponseBody("{}", 1);
  assert(!r1.ok);
  auto r2 = CorrectionService::ParseResponseBody("not json", 1);
  assert(!r2.ok);
  auto r3 = CorrectionService::ParseResponseBody(
      "{\"choices\":[{\"message\":{\"content\":\"plain\"}}]}", 1);
  assert(!r3.ok);
  auto r4 = CorrectionService::ParseResponseBody(
      "{\"choices\":[{\"message\":{\"content\":\"{}\"}}]}", 1);
  assert(!r4.ok);  // no candidates key
}

int main() {
  TestParseBasic();
  TestParseUnicodeEscape();
  TestParseErrors();
  TestDump();
  TestParseChatCompletion();
  TestParseChatCompletionMarkdownFence();
  TestRejectControlChars();
  TestBadResponses();
  std::cout << "all weasel_ai tests passed" << std::endl;
  return 0;
}
