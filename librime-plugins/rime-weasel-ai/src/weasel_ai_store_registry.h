// Engine-keyed registry of AiResultStore instances.
//
// Threading: Rime processes different sessions on different threads, so this
// registry is shared mutable state and MUST be guarded. Entries are never
// removed during process lifetime (one small store per live session); the
// map is bounded by the number of concurrent sessions.

#ifndef WEASEL_AI_STORE_REGISTRY_H_
#define WEASEL_AI_STORE_REGISTRY_H_

#include <map>
#include <mutex>

#include "weasel_ai_result_store.h"

namespace weasel_ai {

class AiStoreRegistry {
 public:
  static AiStoreRegistry& instance() {
    static AiStoreRegistry* registry = new AiStoreRegistry;
    return *registry;
  }

  AiResultStore* FindOrCreate(rime::Engine* engine) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = stores_.find(engine);
    if (it != stores_.end())
      return it->second;
    AiResultStore* store = new AiResultStore;
    stores_[engine] = store;
    return store;
  }

  // Returns nullptr when the engine has no store (never used AI features).
  AiResultStore* Find(rime::Engine* engine) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = stores_.find(engine);
    return it == stores_.end() ? nullptr : it->second;
  }

 private:
  AiStoreRegistry() = default;
  std::mutex mutex_;
  std::map<rime::Engine*, AiResultStore*> stores_;
};

inline AiResultStore* FindOrCreateStore(rime::Engine* engine) {
  return AiStoreRegistry::instance().FindOrCreate(engine);
}

inline AiResultStore* FindStore(rime::Engine* engine) {
  return AiStoreRegistry::instance().Find(engine);
}

}  // namespace weasel_ai

#endif  // WEASEL_AI_STORE_REGISTRY_H_
