// Local logic test for the REAL AiCorrectionFilter, compiled against the
// lightweight rime stubs in test/stubs. It verifies the streaming/insertion
// behaviour that previously could only be checked by rebuilding the whole
// rime.dll in CI.
//
// Covered:
//   * page1_end placement (AI as the Nth output)
//   * "last" placement
//   * upstream AI candidates are dropped and re-created once
//   * duplicate suppression against already-emitted candidates
//   * empty upstream
//   * candidate_position fallback from the deployed default config
//   * multi-segment input matched via the recorded segment range
//   * the recorded menu index (store ai_index) used for highlighting

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>

#include "weasel_ai_filter.h"
#include "weasel_ai_result_store.h"
#include "weasel_ai_store_registry.h"

using rime::an;
using rime::Candidate;
using rime::CandidateList;
using rime::Context;
using rime::Engine;
using rime::Schema;
using rime::SimpleCandidate;
using rime::Ticket;
using rime::Translation;
using weasel_ai::AiCorrectionFilter;
using weasel_ai::AiResult;
using weasel_ai::AiResultStore;
using weasel_ai::FindOrCreateStore;

namespace {

// Mimics rime::FifoTranslation cursor semantics.
class FakeTranslation : public Translation {
 public:
  struct Item {
    std::string text;
    std::string type = "test";
  };
  explicit FakeTranslation(std::vector<std::string> texts) {
    for (auto& t : texts)
      items_.push_back(rime::New<SimpleCandidate>("test", 0, 1, t));
    if (items_.empty())
      set_exhausted(true);
  }
  explicit FakeTranslation(std::vector<Item> items) {
    for (auto& i : items)
      items_.push_back(rime::New<SimpleCandidate>(i.type, 0, 1, i.text));
    if (items_.empty())
      set_exhausted(true);
  }
  bool Next() override {
    if (exhausted())
      return false;
    if (++cursor_ >= items_.size())
      set_exhausted(true);
    return true;
  }
  an<Candidate> Peek() override {
    if (exhausted())
      return nullptr;
    return items_[cursor_];
  }

 private:
  CandidateList items_;
  size_t cursor_ = 0;
};

// Drains exactly like Menu::Prepare does (Peek, push, Next).
std::vector<std::string> Drain(an<Translation> t) {
  std::vector<std::string> out;
  int guard = 0;
  while (t && !t->exhausted()) {
    if (++guard > 1000) {  // hang guard for the test itself
      out.push_back("<<INFINITE-LOOP>>");
      break;
    }
    if (auto c = t->Peek())
      out.push_back(c->text());
    t->Next();
  }
  return out;
}

std::string Join(const std::vector<std::string>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i)
      s += ",";
    s += v[i];
  }
  return s;
}

struct Fixture {
  Engine engine;
  Schema schema;
  Context context;
  AiResultStore* store = nullptr;

  Fixture(bool schema_has_placement = false) {
    rime::Config::Deployed().Clear();
    engine.set_context(&context);
    engine.set_schema(&schema);
    schema.set_page_size(9);  // like the user's menu/page_size
    if (schema_has_placement) {
      // schema-level config: emulate the (unusual) per-schema override
      static rime::Config schema_config;
      schema_config.Clear();
      schema_config.Set("ai_correction/candidate_position", "last");
      schema.set_config(&schema_config);
    }
    store = FindOrCreateStore(&engine);
  }

  void SetResult(const std::string& input,
                 const std::vector<std::string>& texts,
                 size_t start,
                 size_t end,
                 const std::string& comment = "AI校准") {
    AiResult result;
    result.input = input;
    result.seg_start = start;
    result.seg_end = end;
    result.has_range = true;
    result.comment = comment;
    for (auto& t : texts) {
      weasel_ai::AiCandidate c;
      c.text = t;
      result.candidates.push_back(c);
    }
    store->Commit(store->BeginRequest(), std::move(result));
  }

  an<Translation> Apply(std::vector<std::string> upstream) {
    return ApplyItems(FakeTranslation(std::move(upstream)));
  }

  an<Translation> ApplyTyped(std::vector<FakeTranslation::Item> items) {
    return ApplyItems(FakeTranslation(std::move(items)));
  }

  an<Translation> ApplyItems(FakeTranslation upstream) {
    Ticket ticket;
    ticket.engine = &engine;
    ticket.name_space = "ai_correction_filter";
    AiCorrectionFilter filter(ticket);
    CandidateList candidates;
    return filter.Apply(rime::New<FakeTranslation>(std::move(upstream)),
                        &candidates);
  }
};

std::vector<std::string> Normals(int n) {
  std::vector<std::string> v;
  for (int i = 0; i < n; ++i)
    v.push_back("n" + std::to_string(i));
  return v;
}

void TestPage1End() {
  Fixture f;
  rime::Config::Deployed().Set("ai_correction/candidate_position", "page1_end");
  f.context.set_input("nihao");
  f.SetResult("nihao", {"AIresult"}, 0, 5);
  auto out = Drain(f.Apply(Normals(12)));
  // page_size 9 -> insert at index 8
  const std::string want =
      "n0,n1,n2,n3,n4,n5,n6,n7,AIresult,n8,n9,n10,n11";
  assert(Join(out) == want);
  assert(f.store->ai_index() == 8);
  std::cout << "  page1_end OK: " << Join(out) << std::endl;
}

void TestLastPlacement() {
  Fixture f;
  rime::Config::Deployed().Set("ai_correction/candidate_position", "last");
  f.context.set_input("nihao");
  f.SetResult("nihao", {"AIresult"}, 0, 5);
  auto out = Drain(f.Apply(Normals(3)));
  assert(Join(out) == "n0,n1,n2,AIresult");
  assert(f.store->ai_index() == 3);
  std::cout << "  last OK: " << Join(out) << std::endl;
}

void TestUpstreamAiDropped() {
  Fixture f;
  rime::Config::Deployed().Set("ai_correction/candidate_position", "page1_end");
  f.context.set_input("nihao");
  f.SetResult("nihao", {"AIresult"}, 0, 5);
  // The translator injects its own copy (type "ai_correction"); the filter
  // must drop it and insert exactly one AI candidate at the planned index.
  std::vector<FakeTranslation::Item> upstream;
  upstream.push_back({"AIresult", "ai_correction"});
  for (int i = 0; i < 12; ++i)
    upstream.push_back({"n" + std::to_string(i), "test"});
  auto out = Drain(f.ApplyTyped(upstream));
  const std::string joined = Join(out);
  size_t count = 0;
  for (size_t pos = joined.find("AIresult"); pos != std::string::npos;
       pos = joined.find("AIresult", pos + 1))
    ++count;
  assert(count == 1);
  assert(joined == "n0,n1,n2,n3,n4,n5,n6,n7,AIresult,n8,n9,n10,n11");
  std::cout << "  upstream AI deduped OK: " << joined << std::endl;
}

void TestDuplicateSuppressed() {
  Fixture f;
  rime::Config::Deployed().Set("ai_correction/candidate_position", "page1_end");
  rime::Config::Deployed().Set("ai_correction/deduplicate", "1");
  f.context.set_input("nihao");
  f.SetResult("nihao", {"dup"}, 0, 5);
  std::vector<std::string> upstream = Normals(12);
  upstream[2] = "dup";  // already emitted before the insertion point
  auto out = Drain(f.Apply(upstream));
  const std::string joined = Join(out);
  assert(joined.find("dup") != std::string::npos);
  // count must stay 1 (the normal one), the AI copy is suppressed
  size_t count = 0;
  for (size_t pos = joined.find("dup"); pos != std::string::npos;
       pos = joined.find("dup", pos + 1))
    ++count;
  assert(count == 1);
  std::cout << "  duplicate suppressed OK: " << joined << std::endl;
}

void TestDuplicateShownByDefault() {
  // deduplicate is off by default: the AI candidate must be visible even when
  // it repeats an existing candidate (user feedback that the AI ran).
  Fixture f;
  rime::Config::Deployed().Set("ai_correction/candidate_position", "page1_end");
  f.context.set_input("nihao");
  f.SetResult("nihao", {"dup"}, 0, 5);
  std::vector<std::string> upstream = Normals(12);
  upstream[2] = "dup";
  auto out = Drain(f.Apply(upstream));
  const std::string joined = Join(out);
  size_t count = 0;
  for (size_t pos = joined.find("dup"); pos != std::string::npos;
       pos = joined.find("dup", pos + 1))
    ++count;
  assert(count == 2);  // the normal one + the AI one at index 8
  assert(joined == "n0,n1,dup,n3,n4,n5,n6,n7,dup,n8,n9,n10,n11");
  std::cout << "  duplicate shown by default OK: " << joined << std::endl;
}

void TestEmptyUpstream() {
  Fixture f;
  rime::Config::Deployed().Set("ai_correction/candidate_position", "page1_end");
  f.context.set_input("nihao");
  f.SetResult("nihao", {"AIresult"}, 0, 5);
  auto out = Drain(f.Apply({}));
  assert(Join(out) == "AIresult");
  std::cout << "  empty upstream OK: " << Join(out) << std::endl;
}

void TestSchemaOverrideWins() {
  // Per-schema config sets "last"; the deployed default says page1_end.
  Fixture f(/*schema_has_placement=*/true);
  rime::Config::Deployed().Set("ai_correction/candidate_position", "page1_end");
  f.context.set_input("nihao");
  f.SetResult("nihao", {"AIresult"}, 0, 5);
  auto out = Drain(f.Apply(Normals(3)));
  assert(Join(out) == "n0,n1,n2,AIresult");  // last, not index 8
  std::cout << "  schema override OK: " << Join(out) << std::endl;
}

void TestMultiSegmentMatch() {
  Fixture f;
  rime::Config::Deployed().Set("ai_correction/candidate_position", "page1_end");
  // full input longer than the stored segment input
  f.context.set_input("ruguozhendeweilai");  // "weilai" starts at index 11
  f.SetResult("weilai", {"AIresult"}, 11, 17);
  auto out = Drain(f.Apply(Normals(10)));
  const std::string joined = Join(out);
  assert(joined.find("AIresult") != std::string::npos);
  std::cout << "  multi-segment match OK: " << joined << std::endl;
}

void TestNoResultPassthrough() {
  Fixture f;
  f.context.set_input("nihao");
  // no result stored -> upstream must be handed through untouched
  auto out = Drain(f.Apply(Normals(4)));
  assert(Join(out) == "n0,n1,n2,n3");
  std::cout << "  pass-through OK: " << Join(out) << std::endl;
}

void TestStaleResultNotServed() {
  Fixture f;
  rime::Config::Deployed().Set("ai_correction/candidate_position", "page1_end");
  f.context.set_input("biele");  // input changed after the result was stored
  f.SetResult("nihao", {"AIresult"}, 0, 5);
  auto out = Drain(f.Apply(Normals(4)));
  assert(Join(out) == "n0,n1,n2,n3");
  std::cout << "  stale result dropped OK: " << Join(out) << std::endl;
}

void TestCommentFromStore() {
  Fixture f;
  rime::Config::Deployed().Set("ai_correction/candidate_position", "page1_end");
  f.context.set_input("nihao");
  f.SetResult("nihao", {"AIresult"}, 0, 5, "自定义标签");
  Ticket ticket;
  ticket.engine = &f.engine;
  AiCorrectionFilter filter(ticket);
  CandidateList candidates;
  auto t = filter.Apply(rime::New<FakeTranslation>(Normals(10)), &candidates);
  // walk to the AI candidate and check its comment
  std::string comment;
  while (t && !t->exhausted()) {
    auto c = t->Peek();
    if (c && c->type() == "ai_correction")
      comment = c->comment();
    t->Next();
  }
  assert(comment == "自定义标签");
  std::cout << "  comment from store OK: " << comment << std::endl;
}

}  // namespace

int main() {
  TestPage1End();
  TestLastPlacement();
  TestUpstreamAiDropped();
  TestDuplicateSuppressed();
  TestDuplicateShownByDefault();
  TestEmptyUpstream();
  TestSchemaOverrideWins();
  TestMultiSegmentMatch();
  TestNoResultPassthrough();
  TestStaleResultNotServed();
  TestCommentFromStore();
  std::cout << "all filter order tests passed" << std::endl;
  return 0;
}
