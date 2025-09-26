#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <map>
#include <optional>
#include <cassert>
#include <iomanip>
#include <string>
#include "champsim_constants.h"
#include "logger.h"
#include "vmem.h"
#include "user.h"
using namespace std;

// Compile-time sanity
static_assert((PAGE_SIZE & (PAGE_SIZE - 1)) == 0, "PAGE_SIZE must be power of two");
static_assert((BLOCK_SIZE & (BLOCK_SIZE - 1)) == 0, "BLOCK_SIZE must be power of two");
constexpr uint64_t kBlocksPerPage = PAGE_SIZE / BLOCK_SIZE;
static logger pagetable_logger(true);
extern VirtualMemory vmem;

// Utilities
inline constexpr uint64_t page_align(uint64_t addr) { return addr & ~(PAGE_SIZE - 1ULL); }
inline constexpr uint64_t block_index(uint64_t off) { return (off / BLOCK_SIZE) % kBlocksPerPage; }

namespace std {
    template <>
    struct hash<tuple<uint64_t, uint64_t, int>> {
        size_t operator()(const tuple<uint64_t, uint64_t, int>& t) const {
            size_t h1 = hash<uint64_t>{}(get<0>(t));
            size_t h2 = hash<uint64_t>{}(get<1>(t));
            size_t h3 = hash<int>{}(get<2>(t));
            // Combine hashes
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}

class CacheBlock
{
    // default: data page
    bool valid_cacheblock = false;
    int pt_level = -1;
    int cache_block_id = -1;
    vector<uint64_t> list_pte;
    public:
    CacheBlock(){}
    CacheBlock(int curr_pt_level, int cache_block_id)
    {
        list_pte.resize(8, UINT64_MAX);
        this->cache_block_id = cache_block_id;
        valid_cacheblock = true;
    }

    void insertAtCacheBlockPTE(uint64_t pte, int index)
    {
        if(list_pte[index] != UINT64_MAX)
        {
            std::cout << "Error: attempting to overwrite PTE in cacheblock=" << cache_block_id << ", pteindex="<< index << '\n';
            exit(-1);
        }

        list_pte[index] = pte;
    }

    // if value != UINT64_MAX: valid else fault ? and PTE
    pair<bool, uint64_t> get_pte(int index) { return {list_pte[index] != UINT64_MAX, list_pte[index]};}
};

class Page
{
    int page_number = -1;
    //default: data page
    int pt_level = -1;
    map<int, CacheBlock> list_cacheblocks;
    public:

    Page(){}
    Page(int curr_pt_level, int page_number)
    {
        this->pt_level = curr_pt_level;
        this->page_number = page_number;
    }

    void insertAtPage(uint64_t pte, int cache_block_id, int pte_offset)
    {
        auto foundCacheBlock = list_cacheblocks.find(cache_block_id);
        if(foundCacheBlock == list_cacheblocks.end())
        {
            list_cacheblocks[cache_block_id] = CacheBlock(pt_level, cache_block_id);
        }
        list_cacheblocks[cache_block_id].insertAtCacheBlockPTE(pte, pte_offset);        
    }

    pair<bool, uint64_t> get_pte(int cache_block_id, int pte_offset)
    {
        auto foundCacheBlock = list_cacheblocks.find(cache_block_id);
        if(foundCacheBlock == list_cacheblocks.end())
        {
            return {false, 0};
        }
        return foundCacheBlock->second.get_pte(pte_offset);
    }
};

class PageTableTracker
{
    // default: unintialized page-table
    int pt_level = -1;
    map<uint64_t, Page> list_pages;

    public:

    PageTableTracker(int pt_level)
    {
        this->pt_level = pt_level;
    }

    void insertAtPageTable(uint64_t pte_address, uint64_t pte_value, int pt_level)
    {
        int page_number = (pte_address >> LOG2_PAGE_SIZE) & 0xFFF; // 12-bit page number within 4MB range
        int cache_block_id = (pte_address >> LOG2_BLOCK_SIZE) & 0x3F; // 6-bit cache block id within page
        int pte_offset = (pte_address >> 3) & 0x7; // 3-bit offset within cache block

        auto foundPage = list_pages.find(page_number);
        if(foundPage == list_pages.end())
        {
            list_pages[page_number] = Page(pt_level, page_number);
        }
        list_pages[page_number].insertAtPage(pte_value, cache_block_id, pte_offset);
    }

    pair<bool, uint64_t> get_pte(uint64_t pte_address)
    {
        int page_number = (pte_address >> LOG2_PAGE_SIZE) & 0xFFF; // 12-bit page number within 4MB range
        int cache_block_id = (pte_address >> LOG2_BLOCK_SIZE) & 0x3F; // 6-bit cache block id within page
        int pte_offset = (pte_address >> 3) & 0x7; // 3-bit offset within cache block

        auto foundPage = list_pages.find(page_number);
        if(foundPage == list_pages.end())
        {
            // page not found fault
            return {false, 0};
        }
        return list_pages[page_number].get_pte(cache_block_id, pte_offset);
    }
};

class PageTableLevelTracker
{
    int cpu_id = -1;
    vector<PageTableTracker> list_pages_tables_levels;
    public:
    
    PageTableLevelTracker(){}
    PageTableLevelTracker(int cpu_id, int num_pt_levels)
    {
        this->cpu_id = cpu_id;
        list_pages_tables_levels.resize(num_pt_levels+1, PageTableTracker(0));
        for(int i=1; i<= num_pt_levels; i++)
            list_pages_tables_levels[i] = PageTableTracker(i);
    }

    void insertAtPageTableLevel(uint64_t pte_address, uint64_t pte_value, int pt_level)
    {
        if(pt_level < 1 || pt_level >= list_pages_tables_levels.size())
        {
            std::cout << "Error: invalid page-table level=" << pt_level << '\n';
            exit(-1);
        }
        auto& pagetable = list_pages_tables_levels[pt_level];
        pagetable.insertAtPageTable(pte_address, pte_value, pt_level);
    }

    pair<bool, uint64_t> get_pte(uint64_t pte_address, int pt_level)
    {
        pair<bool, uint64_t> found_pte;
        // prefetch packet, shared page between processes or cpus
        if(pt_level < 1 || pt_level >= list_pages_tables_levels.size())
        {
            for(auto entry: list_pages_tables_levels)
            {
                PageTableTracker& page_table_tracker = entry;
                found_pte = page_table_tracker.get_pte(pte_address);
                if(found_pte.first) //found
                    return found_pte;
            }
            pagetable_logger.log("Error: invalid pt-level=-1", "pte_address", pte_address, "pt_level", pt_level, '\n');
            exit(-1);
        }
        
        found_pte = list_pages_tables_levels[pt_level].get_pte(pte_address);
        return found_pte;
    }
};

class ProcessPageTable
{
    unordered_map<int, PageTableLevelTracker> page_table_levels_tracker; // key: cpu or asid of process

    public:
    ProcessPageTable(){}

    // initalize for 16 threads or process
    // initializes for vmem.pt_levels   
    void init()
    {
        for(int i=0; i< 16; i++)
            page_table_levels_tracker[i] = PageTableLevelTracker(i, vmem.pt_levels);
    }

    // TODO: we are not handling ASID here hence using CPU id instead of ASID
    // In future, we can extend this to handle ASID as well hence porcess_id is either ASID or CPU id
    void insert(int process_id, uint64_t pte_address, uint64_t pte_value, int pt_level)
    {
        if(process_id < 0)
        {
            pagetable_logger.log("Error: invalid process/cpu id for inserting a pte", "addr", intToHex(pte_address), "pte_value", intToHex(pte_value), "pt_level", pt_level, '\n');
            exit(-1);
        }
        page_table_levels_tracker[process_id].insertAtPageTableLevel(pte_address, pte_value, pt_level);
    }

    // true -> pte found,       false -> page-fault
    pair<bool, uint64_t> get_pte(int process_id, uint64_t pte_address, int pt_level)
    {
        pair<bool, uint64_t> found_pte;
        // prefetch packet, shared page between processes or cpus
        if(process_id == -1)
        {
            // TODO: make it realistic later
            // virtual prefetches dont walk page table they are just using existing mapping
            for(auto entry: page_table_levels_tracker)
            {
                PageTableLevelTracker& page_table_level_tracker = entry.second;
                found_pte = page_table_level_tracker.get_pte(pte_address, pt_level);
                if(found_pte.first) //found
                    return found_pte;
            }

            pagetable_logger.log("Error: invalid process/cpu id", "cpu", process_id, "addr", pte_address, "pt_level", pt_level, '\n');
            exit(-1);
        }
        return page_table_levels_tracker[process_id].get_pte(pte_address, pt_level);   
    }

    pair<bool, uint64_t> operate_pagetable(int process_id, uint64_t pte_address, int pt_level)
    {
        auto result_pte = get_pte(process_id, pte_address, pt_level);
        if(result_pte.first)
        {
            // found pte
            return {true, result_pte.second};
        }
        
        // page-fault
        uint64_t new_page_addr = vmem.func_allocate_page();
        insert(process_id, pte_address, new_page_addr, pt_level);
        return {true, new_page_addr};
    }   
};

#endif // PAGE_TABLE_H