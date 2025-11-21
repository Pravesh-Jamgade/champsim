#include "DataModel.h"

EvictionTracker::EvictionTracker() = default;

std::pair<std::map<EvictionTracker::Key, int>::iterator, bool>
EvictionTracker::func_track_eviction_data(uint64_t v_addr, int cpuid)
{
    Key key{v_addr, cpuid};
    auto it = pte_eviction_tracker.find(key);
    if (it == pte_eviction_tracker.end()) {
        auto inserted = pte_eviction_tracker.emplace(key, 1);
        return {inserted.first, true};
    }
    ++(it->second);
    return {it, false};
}

bool EvictionTracker::func_lookup_eviction_data(uint64_t v_addr, int cpuid)
{
    Key key{v_addr, cpuid};
    return pte_eviction_tracker.find(key) != pte_eviction_tracker.end();
}
