#include "DataModel.h"

EvictionTracker::EvictionTracker() = default;

std::pair<std::map<EvictionTracker::Key, EvictionTracker::data>::iterator, bool>
EvictionTracker::func_track_eviction_data(uint64_t v_addr, int cpuid, DataType dtype)
{
    Key key{v_addr, cpuid, dtype};
    data d;
    d.evicts = 1;

    auto it = pte_eviction_tracker.find(key);
    if (it == pte_eviction_tracker.end()) {
        auto inserted = pte_eviction_tracker.emplace(key, d);
        return {inserted.first, true};
    }
    ++(it->second.evicts);
    return {it, false};
}

bool EvictionTracker::func_lookup_eviction_data(uint64_t v_addr, int cpuid, DataType dtype)
{
    Key key{v_addr, cpuid, dtype};
    return pte_eviction_tracker.find(key) != pte_eviction_tracker.end();
}

bool EvictionTracker::func_lookup_eviction_over_threshold(uint64_t v_addr, int cpuid, DataType dtype)
{
    Key key{v_addr, cpuid, dtype};
    auto foundEntry = pte_eviction_tracker.find(key);
    if(foundEntry == pte_eviction_tracker.end())
        return false;
    return foundEntry->second.evicts > 2;
}
