#ifndef EVICTION_TRACKER_H
#define EVICTION_TRACKER_H
#include <iostream>
#include <map>
#include <inttypes.h>
using namespace std;

// key and eviction count
map<tuple<uint64_t, int>, int> pte_eviction_tracker;

// vpage (tlb) or vblock address ()
// iterator and inserted or not --> 
// true means first time eviction seen and inserted to track, 
// false means eviction seen earlier 
// updates the eviction count
pair<map<tuple<uint64_t, int>, int>::iterator, bool> 
    func_track_eviction_data(uint64_t v_addr, int cpuid)
{
    tuple<uint64_t, int> key = {v_addr, cpuid};
    auto found = pte_eviction_tracker.find(key);
    if(found == pte_eviction_tracker.end())
    {
        auto inserted = pte_eviction_tracker.emplace(key, 0);
        return {inserted.first, true};
    }
    found->second++;
    return {found, false};
}

// True - found
// False - no found
bool func_lookup_eviction_data(uint64_t v_addr, int cpuid)
{
    tuple<uint64_t, int> key = {v_addr, cpuid};
    auto found = pte_eviction_tracker.find(key);
    return found != pte_eviction_tracker.end();
}
#endif