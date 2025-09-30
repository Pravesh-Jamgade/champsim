#ifndef POMTLB_H
#define POMTLB_H
#include <bits/stdc++.h>
#include "champsim_constants.h"
#include "logger.h"
#include "vmem.h"
using namespace std;

#define POMSETS 256
#define POMWAYS 4
#define PTE_LIMIT 8 // no of pte in tuple
#define CPUS 16

class POMTLB
{
    public:
    // 8 pte in a cacheblock: vp,pp,lru
    using PTE = array<tuple<uint64_t, uint64_t>, 8>;
    // way with tag and cacheblock
    using WAYS = array<tuple<uint64_t, PTE, int>, POMWAYS>;
    // set of 4 ways
    using SETS = array<WAYS, POMSETS>;
    SETS sets_ways_ptes[CPUS];

    POMTLB()
    {
        for(int i=0; i< 16; i++)
        {
            sets_ways_ptes[i] = SETS();
        }
    }

    // tag, set_index, pte_offset
    tuple<int, int, int> split_address(uint64_t addr)
    {
        // pte index, MSB 3 bits from 64 byte block
        int block_offset = (addr >> 3) & 0x7;
        // get set index
        int set_index = (addr >> 6) & (~(POMSETS - 1));
        int tag = addr >> (6 + lg2(POMSETS));
        return {tag, set_index, block_offset};
    }

    pair<bool, uint64_t> lookupPOMEntry(int process_id, uint64_t pte_address, uint64_t virt_address)  
    {
        auto [tag, set_index, pte_offset] = split_address(pte_address);

        // array of sets
        SETS& all_sets = sets_ways_ptes[process_id];

        // array of ways for given set
        auto& all_ways = all_sets[set_index]; 
        
        // check if tag matches any way
        for(auto& [way_tag, ptes, lru]: all_ways)
        {
            auto& [vp, pp] = ptes[pte_offset];

            if(way_tag == tag && vp == page_align(virt_address)) // hit
            {
                lru = 0;

                // update lru of other ways
                for(auto& [other_way_tag, other_ptes, other_lru]: all_ways)
                {
                    if(other_way_tag != way_tag)
                    {
                        other_lru++;
                    }       
                }

                {
                    return {true, pp};
                }   
            }
        }

        return {false, 0};
    }

    // our POM-TLB "x" sets and 4 ways
    void insertPOMEntry(int process_id, uint64_t pte_address, uint64_t pte_value, uint64_t virt_address)
    {
        auto [tag, set_index, pte_offset] = split_address(pte_address);

        // array of sets
        SETS& all_sets = sets_ways_ptes[process_id];

        // array of ways for given set
        auto& all_ways = all_sets[set_index]; 
        
        // check if tag matches any way
        for(auto& [way_tag, ptes, lru]: all_ways)
        {
            auto [vp, pp] = ptes[pte_offset];
            vp = page_align(virt_address); 
            pp = page_align(pte_value);

            if(way_tag == tag && vp == page_align(virt_address)) // hit
            {
                lru = 0;

                // update lru of other ways
                for(auto& [other_way_tag, other_ptes, other_lru]: all_ways)
                {
                    if(other_way_tag != way_tag)
                    {
                        other_lru++;
                    }       
                }

                // update pte_offset entry
                {
                    ptes[pte_offset] = {vp, pp};
                }   
                return;
            }
        }

        if(all_ways.size() >= POMWAYS)
        {
            // evict lru way
            auto lru_way = std::max_element(all_ways.begin(), all_ways.end(), [](const auto& a, const auto& b){
                return std::get<2>(a) < std::get<2>(b);
            });

            (*lru_way) = {tag, {}, 0};
            auto& [way_tag, ptes, lru] = *lru_way;
            ptes[pte_offset] = {page_align(virt_address), page_align(pte_value)}; 

            // update lru of other ways
            for(auto& [other_way_tag, other_ptes, other_lru]: all_ways)
            {
                if(other_way_tag != tag)
                {
                    other_lru++;
                }       
            }
        }
        else
        {
            // insert new way
            PTE new_pte = {};
            new_pte[pte_offset] = {page_align(virt_address), page_align(pte_value)};
            all_ways.fill({tag, new_pte, 0});

            // update lru of other ways
            for(auto& [other_way_tag, other_ptes, other_lru]: all_ways)
            {
                if(other_way_tag != tag)
                {
                    other_lru++;
                }       
            }
        }
    }
    
};
#endif