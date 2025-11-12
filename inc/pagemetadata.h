#ifndef PAGE_META_H
#define PAGE_META_H

#include <bits/stdc++.h>
#include "logger.h"
using namespace std;

/// @brief  Goal is to track moment of page table,translation cache block and pte at TLB and data-cache
/// Sector gains speedup. Though there are zero reuse PTE on STLB. So where this speedup comming from ?, likely from tblock spatial locality
class PageMetaData
{
    public:
    
    logger dlog = logger(true);
    int index_of_tblock_upon_init_access_within_page = -1;
    int index_of_pte_upon_init_access_within_tblock = -1;
    int tblock_reaccessed_more_than_once = 0;
    int tblock_reaccessed_init_pte = 0;
    // second TLB miss leads to access to tblock in cache hierarhy, if tblock still available it will hit
    // first: cache-level, second: array[pteindex]->freq
    map<int, array<int,8>> access_freq_of_all_valid_pte_upon_second_tlbmiss;

    // TODO
    int re_access_disatnce_by_mem_ref_counting = 0;

    PageMetaData(){}

    // block, pte init for first access which cause the block to brought in
    PageMetaData(int init_block, int init_pte, int mem_ref=0): 
        index_of_tblock_upon_init_access_within_page(init_block), 
        index_of_pte_upon_init_access_within_tblock(init_pte),
        // TODO
        re_access_disatnce_by_mem_ref_counting(mem_ref)
    {}

    void update_second_access(uint64_t virt_address, int cache_id)
    {
        int pte_offset = (virt_address >> 3) & 0x7; // 3-bit offset within cache block

        // tblock reaccess how often ?
        tblock_reaccessed_more_than_once++;

        // tblock reaccess to whcih pte ?
        access_freq_of_all_valid_pte_upon_second_tlbmiss[cache_id][pte_offset]++;

        // how often tblock reaccess to same PTE which caused the tblock to be cached ?
        if(pte_offset == index_of_pte_upon_init_access_within_tblock) 
            tblock_reaccessed_init_pte++;
    }

    void print_stats()
    {
    
    }
};

#endif