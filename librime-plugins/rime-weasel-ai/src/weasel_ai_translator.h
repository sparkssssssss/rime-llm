// Weasel AI correction - translator that surfaces the AI result as a
// candidate. The actual request is performed by the AiCorrectionProcessor
// (explicit user trigger); this translator reads the stored result and
// wraps it in a SimpleCandidate. It never blocks on network I/O.

#ifndef WEASEL_AI_TRANSLATOR_H_
#define WEASEL_AI_TRANSLATOR_H_

#include <rime/common.h>
#include <rime/segmentation.h>
#include <rime/translation.h>
#include <rime/translator.h>

namespace weasel_ai {

class AiResultStore;

class AiCorrectionTranslator : public rime::Translator {
 public:
  explicit AiCorrectionTranslator(const rime::Ticket& ticket);

  rime::an<rime::Translation> Query(const rime::string& input,
                                    const rime::Segment& segment) override;

 private:
  rime::the<AiResultStore> store_;
};

}  // namespace weasel_ai

#endif  // WEASEL_AI_TRANSLATOR_H_
