#ifndef EVICTION_TRACKER_H
#define EVICTION_TRACKER_H
#include <iostream>
#include <map>
#include <inttypes.h>
using namespace std;

class PTEEvictionTracker
{
    // key and eviction count
    map<tuple<uint64_t, int>, int> pte_eviction_tracker;

    public:

    PTEEvictionTracker(){}

    // vpage (tlb) or vblock address ()
    // iterator and inserted or not --> 
    // true means first time eviction seen and inserted to track, 
    // false means eviction seen earlier 
    // updates the eviction count
    inline pair<map<tuple<uint64_t, int>, int>::iterator, bool> 
        func_track_eviction_data(uint64_t v_addr, int cpuid);

    // True - found
    // False - no found
    bool func_lookup_eviction_data(uint64_t v_addr, int cpuid);
};

#endif