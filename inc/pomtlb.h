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

static logger pom_logger(false);


struct PTELookup { bool hit; uint64_t value; array<tuple<uint64_t, uint64_t>, 8> ptes;};

enum POMFLAG
{
    // STLB-miss now send POM request 
    POM_FIRST_REQ=0,
    // POM request failed switch to default PTW request
    POM_SECOND_REQ,
    // POM succeed 
    POM_SUCCESS,
    POMFLAG_END
};


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

    int pom_counters[POMFLAG::POMFLAG_END] = {0};

    POMTLB()
    {
        for(int i=0; i< 16; i++)
        {
            sets_ways_ptes[i] = SETS();
        }
    }
    
    ~POMTLB()
    {

    }

    #include <iostream>
#include <iomanip>
#include <tuple>
#include <array>

// assumes your typedefs:
// using PTE  = std::array<std::tuple<uint64_t,uint64_t>, 8>;
// using WAYS = std::array<std::tuple<uint64_t,PTE,int>, POMWAYS>;
// using SETS = std::array<WAYS, POMSETS>;
// extern SETS sets_ways_ptes[CPUS];

static inline bool nonzero_pte(const std::tuple<uint64_t,uint64_t>& t) {
    return std::get<0>(t) || std::get<1>(t);
}

void dump_pom(const SETS (&tbl)[CPUS], std::ostream& os = std::cout,
              bool only_nonzero = true)
{
    os << std::hex << std::showbase;   // hex for addrs
    for (size_t cpu = 0; cpu < CPUS; ++cpu) {
        os << "CPU " << cpu << ":\n";
        for (size_t set = 0; set < POMSETS; ++set) {
            const WAYS& ways = tbl[cpu][set];
            for (size_t w = 0; w < POMWAYS; ++w) {
                const auto& way = ways[w];
                const uint64_t tag = std::get<0>(way);
                const PTE&     pte = std::get<1>(way);
                const int      lru = std::get<2>(way);

                // If all 8 entries are zero and tag is zero, skip when only_nonzero
                if (only_nonzero) {
                    bool any = tag != 0;
                    if (!any) {
                        for (size_t i = 0; i < pte.size(); ++i) {
                            if (nonzero_pte(pte[i])) { any = true; break; }
                        }
                    }
                    if (!any) continue;
                }

                os << "  set " << std::dec << set
                   << "  way " << w
                   << "  tag " << std::hex << tag
                   << "  lru " << std::dec << lru << "\n";

                for (size_t i = 0; i < pte.size(); ++i) {
                    const uint64_t vp = std::get<0>(pte[i]);
                    const uint64_t pp = std::get<1>(pte[i]);
                    if (only_nonzero && vp == 0 && pp == 0) continue;

                    os << "    pte[" << std::dec << i << "]: "
                       << "vp=" << std::hex << vp
                       << "  pp=" << std::hex << pp << "\n";
                }
            }
        }
    }
    os << std::dec << std::noshowbase; // restore formatting
}

    void print_stats()
    {
        cout << "\n==============================================================\n";
        dump_pom(sets_ways_ptes);
        cout << "\n==============================================================\n";
        cout << "POM Request Sent, " << pom_counters[POMFLAG::POM_FIRST_REQ] << '\n';
        cout << "POM Request Success, " << pom_counters[POMFLAG::POM_SUCCESS] << '\n';
        cout << "POM To PTW Switch, " << pom_counters[POMFLAG::POM_SECOND_REQ] << '\n';
        cout << "\n==============================================================\n";
    }

    // tag, set_index, pte_offset
    tuple<int, int, int> split_address(uint64_t addr)
    {
        // pte index, MSB 3 bits from 64 byte block
        int block_offset = (addr >> 3) & 0x7;
        // get set index
        int set_index = (addr >> 6) & (POMSETS - 1);
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

            // found tag
            if(way_tag == tag)// && vp == page_align(virt_address)) // hit
            {
                // update lru of other ways
                for(auto& [other_way_tag, other_ptes, other_lru]: all_ways)
                {
                    // avoid the same entry
                    // check if we have less lru value than matched one
                    if(other_way_tag != way_tag && other_lru < lru)
                    {
                        other_lru++;
                    }       
                }
                
                pom_logger.log("POM hit", intToHex(pte_address), intToHex(virt_address), intToHex(vp), intToHex(pp),"set", set_index, "offset", pte_offset, '\n');
                // reset our lru
                lru = 0;
                return {true, pp};
            }
        }

        pom_logger.log("POM miss", intToHex(pte_address), intToHex(virt_address), "set", set_index, "offset", pte_offset, '\n');

        return {false, 0};
    }   

    // get POM-TLB line
    PTELookup getPOMTLBLine(int process_id, uint64_t pte_address, uint64_t virt_address)  
    {
        PTELookup retPTE;
        auto [tag, set_index, pte_offset] = split_address(pte_address);

        // array of sets
        SETS& all_sets = sets_ways_ptes[process_id];

        // array of ways for given set
        auto& all_ways = all_sets[set_index]; 
        
        // check if tag matches any way
        for(auto& [way_tag, ptes, lru]: all_ways)
        {
            auto& [vp, pp] = ptes[pte_offset];

            // found tag
            if(way_tag == tag)
            {
                // update lru of other ways
                for(auto& [other_way_tag, other_ptes, other_lru]: all_ways)
                {
                    // avoid the same entry
                    // check if we have less lru value than matched one
                    if(other_way_tag != way_tag && other_lru < lru)
                    {
                        other_lru++;
                    }       
                }
                
                pom_logger.log("POM hit", intToHex(pte_address), intToHex(virt_address), "vpage", intToHex(vp), "PTE", intToHex(pp), '\n');
                // reset our lru
                lru = 0;

                return {true, pp, ptes};
            }
        }

        pom_logger.log("POM miss", intToHex(pte_address), intToHex(virt_address), '\n');

        return {false, 0, {}};
    }

    // our POM-TLB "x" sets and 4 ways
    void insertPOMEntry(int process_id, uint64_t pte_address, uint64_t pte_value, uint64_t virt_address)
    {
        auto [tag, set_index, pte_offset] = split_address(pte_address);
        pom_logger.log("POM INSERT", intToHex(pte_address), "vpage", intToHex(page_align(virt_address)), "PTE", intToHex(pte_value), "set", set_index, "offset", pte_offset, '\n');

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

            // found match
            if(way_tag == tag && vp == page_align(virt_address)) // hit
            {
                // update lru of other ways
                for(auto& [other_way_tag, other_ptes, other_lru]: all_ways)
                {
                    if(other_way_tag != way_tag && other_lru < lru)
                    {
                        other_lru++;
                    }       
                }

                lru = 0;
                ptes[pte_offset] = {vp, pp};
                return;
            }
        }

        // no match, insert new entry
        if(all_ways.size() >= POMWAYS)
        {
            // evict lru way
            auto lru_way = std::max_element(all_ways.begin(), all_ways.end(), [](const auto& a, const auto& b){
                return std::get<2>(a) < std::get<2>(b);
            });

            // reset LRU location with new tag
            (*lru_way) = {tag, {}, 0};
            auto& [way_tag, ptes, lru] = *lru_way;
            ptes[pte_offset] = {page_align(virt_address), page_align(pte_value)}; 
        }
        else
        {
            // insert new way
            PTE new_pte = {};
            new_pte[pte_offset] = {page_align(virt_address), page_align(pte_value)};
            all_ways.fill({tag, new_pte, 0});
        }

        // update lru of other ways
        for(auto& [other_way_tag, other_ptes, other_lru]: all_ways)
        {
            if(other_way_tag != tag)
            {
                other_lru++;
            }       
        }
    }
    
};
#endif