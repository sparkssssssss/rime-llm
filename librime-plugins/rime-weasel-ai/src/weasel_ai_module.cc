// Weasel AI correction - librime module registration.
//
// Registers three schema components:
//   ai_correction_processor  - trigger key handler (place FIRST in
//                              engine/processors so it sees keys before
//                              speller/selector consume them)
//   ai_correction_translator - exposes stored AI results as candidates
//                              (place LAST in engine/translators)
//   ai_correction_filter     - pushes AI candidates to the end, dedupes
//                              (place LAST in engine/filters, after
//                              uniquifier/simplifier)
//
// The module is merged into rime.dll by librime's plugin build (plugins/
// auto-glob + RIME_EXTRA_MODULES); no runtime DLL loading is needed on
// Windows.

#include <rime_api.h>
#include <rime/common.h>
#include <rime/registry.h>

#include "weasel_ai_filter.h"
#include "weasel_ai_processor.h"
#include "weasel_ai_translator.h"

using namespace rime;

static void rime_weasel_ai_initialize() {
  LOG(INFO) << "registering components from module 'weasel_ai'.";
  Registry& r = Registry::instance();
  r.Register("ai_correction_processor",
             new Component<weasel_ai::AiCorrectionProcessor>);
  r.Register("ai_correction_translator",
             new Component<weasel_ai::AiCorrectionTranslator>);
  r.Register("ai_correction_filter",
             new Component<weasel_ai::AiCorrectionFilter>);
}

static void rime_weasel_ai_finalize() {}

RIME_REGISTER_MODULE(weasel_ai)
