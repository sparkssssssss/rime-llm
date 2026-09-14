// Unit tests for the AI candidate placement planner (pure logic).
#include <cassert>
#include <iostream>
#include <vector>

#include "../src/weasel_ai_placement.h"

using weasel_ai::AiPlacement;
using weasel_ai::EmitAction;

namespace {

// Simulates streaming: `normal` upstream candidates, `ai` AI candidates.
std::vector<char> Run(size_t insert_index, size_t ai, size_t normal) {
  AiPlacement plan(insert_index, ai);
  std::vector<char> out;
  size_t emitted = 0;
  size_t upstream_done = 0;
  for (int guard = 0; guard < 10000; ++guard) {
    const bool up_exhausted = upstream_done >= normal;
    EmitAction action = plan.Next(emitted, up_exhausted);
    if (action == EmitAction::kDone)
      break;
    if (action == EmitAction::kAi) {
      out.push_back('A');
      ++emitted;
    } else {
      out.push_back('n');
      ++upstream_done;
      ++emitted;
    }
  }
  return out;
}

std::string Str(const std::vector<char>& v) {
  return std::string(v.begin(), v.end());
}

void TestAppendLast() {
  // insert_index = kLast -> AI after all normals
  assert(Str(Run(AiPlacement::kLast, 1, 5)) == "nnnnnA");
  assert(Str(Run(AiPlacement::kLast, 2, 3)) == "nnnAA");
  assert(Str(Run(AiPlacement::kLast, 1, 0)) == "A");
}

void TestInsertAtPageEnd() {
  // page1_end with page_size 5 -> index 4: after 4 normals
  assert(Str(Run(4, 1, 100)) == "nnnnA" + std::string(96, 'n'));
  assert(Str(Run(4, 1, 4)) == "nnnnA");
  assert(Str(Run(4, 1, 2)) == "nnA");       // fewer normals than index
  assert(Str(Run(4, 1, 0)) == "A");
  assert(Str(Run(4, 2, 2)) == "nnAA");      // second AI appended at end
}

void TestMultipleAiAtEnd() {
  assert(Str(Run(AiPlacement::kLast, 3, 2)) == "nnAAA");
  // index 0: the FIRST AI is emitted first, further AI candidates go last
  assert(Str(Run(0, 2, 2)) == "AnnA");
}

void TestEmpty() {
  assert(Str(Run(AiPlacement::kLast, 0, 3)) == "nnn");
  assert(Str(Run(AiPlacement::kLast, 0, 0)) == "");
}

}  // namespace

int main() {
  TestAppendLast();
  TestInsertAtPageEnd();
  TestMultipleAiAtEnd();
  TestEmpty();
  std::cout << "all placement tests passed" << std::endl;
  return 0;
}
