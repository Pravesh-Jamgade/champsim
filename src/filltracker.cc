#include "DataModel.h"

FillTracker::FillTracker() = default;

std::pair<std::map<FillTracker::Key, FillTracker::data>::iterator, bool>
FillTracker::func_track_fill_data(uint64_t v_addr, int cpuid, DataType dtype)
{
    struct data {
        bool valid=false;
        int type=DataType::INVALID;
        int fills = 0;
    };

    Key key{v_addr, cpuid};
    auto it = page_and_cache_block_tracker.find(key);
    // page or block not found
    if (it == page_and_cache_block_tracker.end()) {
        auto inserted = page_and_cache_block_tracker.emplace(key, data{true, dtype, 1} );
        return {inserted.first, true};
    }
    // page or block found
    ++(it->second.fills);
    return {it, false};
}

std::pair<std::map<FillTracker::Key, FillTracker::data>::iterator, bool> 
FillTracker::func_lookup_fill_data(uint64_t v_addr, int cpuid)
{
    Key key{v_addr, cpuid};
    auto it = page_and_cache_block_tracker.find(key);
    return {it, it != page_and_cache_block_tracker.end()};
}
