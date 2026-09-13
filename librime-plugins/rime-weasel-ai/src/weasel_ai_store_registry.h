// Engine-keyed registry of AiResultStore instances.
// Each Rime session has its own Engine; processors/translator/filters of the
// same engine must share one store. librime has no engine-attached userdata
// slot, so we keep a weak map here. Entries are never removed during engine
// lifetime; the map is tiny (one entry per active session) and bounded by
// the number of concurrent sessions.

#ifndef WEASEL_AI_STORE_REGISTRY_H_
#define WEASEL_AI_STORE_REGISTRY_H_

#include <map>

#include "weasel_ai_result_store.h"

namespace weasel_ai {

inline AiResultStore* FindOrCreateStore(rime::Engine* engine) {
  static std::map<rime::Engine*, AiResultStore*>& registry =
      *new std::map<rime::Engine*, AiResultStore*>;
  auto it = registry.find(engine);
  if (it != registry.end())
    return it->second;
  auto* store = new AiResultStore;
  registry[engine] = store;
  return store;
}

}  // namespace weasel_ai

#endif  // WEASEL_AI_STORE_REGISTRY_H_
