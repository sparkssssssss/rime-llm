// Engine-keyed registry of AiResultStore instances.
//
// OWNERSHIP: the registry owns the stores (unique_ptr). Components must only
// borrow the raw pointer - taking ownership in a component (e.g. storing it
// in a `the<>`/unique_ptr) leads to the store being freed while the registry
// and other components still reference it, i.e. use-after-free and double
// free when a schema switch or redeploy rebuilds the engine.
//
// Threading: Rime processes different sessions on different threads, so this
// registry is shared mutable state and MUST be guarded. Entries are never
// removed during process lifetime (one small store per live session); the
// map is bounded by the number of sessions seen.

#ifndef WEASEL_AI_STORE_REGISTRY_H_
#define WEASEL_AI_STORE_REGISTRY_H_

#include <map>
#include <memory>
#include <mutex>

#include "weasel_ai_result_store.h"

namespace weasel_ai {

class AiStoreRegistry {
 public:
  static AiStoreRegistry& instance() {
    static AiStoreRegistry* registry = new AiStoreRegistry;
    return *registry;
  }

  // Returns a borrowed pointer; ownership stays with the registry.
  AiResultStore* FindOrCreate(rime::Engine* engine) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = stores_.find(engine);
    if (it != stores_.end())
      return it->second.get();
    std::unique_ptr<AiResultStore> store(new AiResultStore);
    AiResultStore* borrowed = store.get();
    stores_.emplace(engine, std::move(store));
    return borrowed;
  }

  // Returns nullptr when the engine has no store (never used AI features).
  AiResultStore* Find(rime::Engine* engine) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = stores_.find(engine);
    return it == stores_.end() ? nullptr : it->second.get();
  }

 private:
  AiStoreRegistry() = default;
  std::mutex mutex_;
  std::map<rime::Engine*, std::unique_ptr<AiResultStore>> stores_;
};

inline AiResultStore* FindOrCreateStore(rime::Engine* engine) {
  return AiStoreRegistry::instance().FindOrCreate(engine);
}

inline AiResultStore* FindStore(rime::Engine* engine) {
  return AiStoreRegistry::instance().Find(engine);
}

}  // namespace weasel_ai

#endif  // WEASEL_AI_STORE_REGISTRY_H_
