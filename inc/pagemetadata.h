#ifndef PAGE_META_H
#define PAGE_META_H

#include <bits/stdc++.h>
#include "logger.h"
#include "DataModel.h"
using namespace std;

/// @brief  Goal is to track movement translation cache block and pte at TLB and data-cache
/// Sector gains speedup. Though there are zero reuse PTE on STLB. So where this speedup comming from ?, likely from tblock spatial locality
class TblockMetaData
{
    public:
    
    logger dlog = logger(true);
    int index_of_tblock_upon_init_access_within_page = -1;
    int index_of_pte_upon_init_access_within_tblock = -1;
    int tblock_accesses = 0;
    map<int, int> tblock_accesses_cache_loc;
    map<int, int> tblock_eviction_cache_loc;

    int tblock_evicted = 0;
    // should be same as "tblock_accesses"
    int tlb_reuse = 0;

    TblockMetaData(){}

    // block, pte init for first access which cause the block to brought in
    TblockMetaData(int init_block, int init_pte, int mem_ref=0): 
        index_of_tblock_upon_init_access_within_page(init_block), 
        index_of_pte_upon_init_access_within_tblock(init_pte)
    {}

    // number of times tblock is reused
    void update_second_access(uint64_t virt_address, int cache_id)
    {
        // number of times tblock is reused
        tblock_accesses++;
        tblock_accesses_cache_loc[cache_id]++;
    }

    void update_eviction(int cache_id)
    {
        tblock_evicted++;
        tblock_eviction_cache_loc[cache_id]++;
    }

    void print_stats()
    {
        // cout << "============================================================\n";
        // cout << "           TBlock          \n";
        // cout << "tblock reused, " << tblock_accesses << '\n';
        // cout << "tblock re-accessed for evicted PTE from stlb\n";
        // for(auto entry: tblock_reaccessed_init_pte)
        // {
        //     cout << hit_where_str[entry.first] << ", " <<  entry.second << '\n';
        // }
        // cout << "tblock re-accessed for spatial locality\n";
        // for(auto entry: access_freq_of_all_valid_pte_upon_second_tlbmiss)
        // {
        //     cout << "At " << hit_where_str[entry.first] << '\n';
        //     for(int i=0; i< 8; i++)
        //     {
        //         cout << "block-" << i << ", " << entry.second[i] << '\n'; 
        //     }
        // }
        // cout << "============================================================\n";
    }
};

#endif