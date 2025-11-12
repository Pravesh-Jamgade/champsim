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
#include <bitset>
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

class PTEHolder
{
    public:
    uint64_t page_address = UINT64_MAX;
    uint64_t virt_page_address = UINT64_MAX;
    int dram_accesses_during_ptw = 0;
    int number_of_ptw = 0;

    PTEHolder(){}
    PTEHolder(uint64_t pte): page_address(pte) {}
    void inc_dram_count(){dram_accesses_during_ptw++;}
    void inc_ptw_count(){number_of_ptw++;}
};

class CacheBlock
{
    // default: data page
    bool valid_cacheblock = false;
    int pt_level = -1;
    int cache_block_id = -1;
    
    public:
    vector<PTEHolder> list_pte;

    CacheBlock(){}
    CacheBlock(int curr_pt_level, int cache_block_id)
    {
        list_pte.resize(8);
        this->cache_block_id = cache_block_id;
        valid_cacheblock = true;
    }

    void insertAtCacheBlockPTE(uint64_t pte, int index, uint64_t virt_page)
    {
        if(list_pte[index].page_address != UINT64_MAX)
        {
            std::cout << "Error: attempting to overwrite PTE in cacheblock=" << cache_block_id << ", pteindex="<< index << '\n';
            exit(-1);
        }

        list_pte[index].page_address = pte;
        list_pte[index].virt_page_address = virt_page;
    }

    // if value != UINT64_MAX: valid else fault ? and PTE
    pair<bool, PTEHolder> get_pte(int index) { return {list_pte[index].page_address != UINT64_MAX, list_pte[index]};}
};

class Page
{
    //default: data page
    int pt_level = -1;
    
    public:
    int page_number = -1;
    map<int, CacheBlock> list_cacheblocks;

    Page(){}
    Page(int curr_pt_level, int page_number)
    {
        this->pt_level = curr_pt_level;
        this->page_number = page_number;
    }

    void insertAtPage(uint64_t pte, int cache_block_id, int pte_offset, uint64_t virt_page)
    {
        auto foundCacheBlock = list_cacheblocks.find(cache_block_id);
        if(foundCacheBlock == list_cacheblocks.end())
        {
            list_cacheblocks[cache_block_id] = CacheBlock(pt_level, cache_block_id);
        }
        list_cacheblocks[cache_block_id].insertAtCacheBlockPTE(pte, pte_offset, virt_page);        
    }

    pair<bool, PTEHolder> get_pte(int cache_block_id, int pte_offset)
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

    public:
    map<uint64_t, Page> list_pages;

    PageTableTracker(int pt_level)
    {
        this->pt_level = pt_level;
    }

    void insertAtPageTable(uint64_t pte_address, uint64_t pte_value, int pt_level, uint64_t virt_address)
    {
        int page_number = (pte_address >> LOG2_PAGE_SIZE); // 12-bit page number within 4MB range
        int cache_block_id = (pte_address >> LOG2_BLOCK_SIZE) & 0x3F; // 6-bit cache block id within page
        int pte_offset = (pte_address >> 3) & 0x7; // 3-bit offset within cache block

        auto foundPage = list_pages.find(page_number);
        if(foundPage == list_pages.end())
        {
            list_pages[page_number] = Page(pt_level, page_number);
        }

        list_pages[page_number].insertAtPage(pte_value, cache_block_id, pte_offset, virt_address);
    }

    pair<bool, PTEHolder> get_pte(uint64_t pte_address)
    {
        int page_number = (pte_address >> LOG2_PAGE_SIZE); // 12-bit page number within 4MB range
        int cache_block_id = (pte_address >> LOG2_BLOCK_SIZE) & 0x3F; // 6-bit cache block id within page
        int pte_offset = (pte_address >> 3) & 0x7; // 3-bit offset within cache block

        auto foundPage = list_pages.find(page_number);
        if(foundPage == list_pages.end())
        {
            // page not found fault
            return {false, PTEHolder()};
        }
        return list_pages[page_number].get_pte(cache_block_id, pte_offset);
    }
};

class PageTableLevelTracker
{
    int cpu_id = -1;
    
    public:
    vector<PageTableTracker> list_pages_tables_levels;
    
    PageTableLevelTracker(){}
    PageTableLevelTracker(int cpu_id, int num_pt_levels)
    {
        this->cpu_id = cpu_id;
        list_pages_tables_levels.resize(num_pt_levels+1, PageTableTracker(0));
        for(int i=1; i<= num_pt_levels; i++)
            list_pages_tables_levels[i] = PageTableTracker(i);
    }

    void insertAtPageTableLevel(uint64_t pte_address, uint64_t pte_value, int pt_level, uint64_t virt_address)
    {
        if(pt_level < 1 || pt_level >= list_pages_tables_levels.size())
        {
            std::cout << "Error: invalid page-table level=" << pt_level << '\n';
            exit(-1);
        }
        auto& pagetable = list_pages_tables_levels[pt_level];
        pagetable.insertAtPageTable(pte_address, pte_value, pt_level, virt_address);
    }

    pair<bool, PTEHolder> get_pte(uint64_t pte_address, int pt_level)
    {
        pair<bool, PTEHolder> found_pte;
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
    unordered_map<int, PageTableLevelTracker> process_to_pagetable_levels_tracker; // key: cpu or asid of process

    public:
    ProcessPageTable(){}

    // initalize for 16 threads or process
    // initializes for vmem.pt_levels   
    void init()
    {
        for(int i=0; i< 16; i++)
            process_to_pagetable_levels_tracker[i] = PageTableLevelTracker(i, vmem.pt_levels);
    }

    // TODO: we are not handling ASID here hence using CPU id instead of ASID
    // In future, we can extend this to handle ASID as well hence porcess_id is either ASID or CPU id
    void insert(int process_id, uint64_t pte_address, uint64_t pte_value, int pt_level, uint64_t virt_address)
    {
        if(process_id < 0)
        {
            pagetable_logger.log("Error: invalid process/cpu id for inserting a pte", "addr", intToHex(pte_address), "pte_value", intToHex(pte_value), "pt_level", pt_level, '\n');
            exit(-1);
        }

        process_to_pagetable_levels_tracker[process_id].insertAtPageTableLevel(pte_address, pte_value, pt_level, virt_address);
    }

    // true -> pte found,       false -> page-fault
    pair<bool, PTEHolder> get_pte(int process_id, uint64_t pte_address, int pt_level)
    {
        pair<bool, PTEHolder> found_pte;
        // prefetch packet, shared page between processes or cpus
        if(process_id == -1)
        {
            // TODO: make it realistic later
            // virtual prefetches dont walk page table they are just using existing mapping
            for(auto entry: process_to_pagetable_levels_tracker)
            {
                PageTableLevelTracker& page_table_level_tracker = entry.second;
                found_pte = page_table_level_tracker.get_pte(pte_address, pt_level);
                if(found_pte.first) //found
                    return found_pte;
            }

            pagetable_logger.log("Error: invalid process/cpu id", "cpu", process_id, "addr", pte_address, "pt_level", pt_level, '\n');
            exit(-1);
        }
        return process_to_pagetable_levels_tracker[process_id].get_pte(pte_address, pt_level);   
    }

    // True->page_found and False->page_not_found
    pair<bool, PTEHolder> operate_pagetable(int process_id, uint64_t pte_address, int pt_level, uint64_t virt_address)
    {
        auto result_pte = get_pte(process_id, pte_address, pt_level);

        // pagetable_logger.log("Operate", "cpu", process_id, "addr", intToHex(pte_address), "pt_level", pt_level, "fault", !result_pte.first, "data", intToHex(result_pte.second.page_address), '\n');

        if(result_pte.first)
        {
            // found pte
            return {true, result_pte.second};
        }
        
        // page-fault
        uint64_t new_page_addr = vmem.func_allocate_page();
        // pagetable_logger.log("InsertPTE", "cpu", process_id, "addr", intToHex(pte_address), "pt_level", pt_level, "fault", !result_pte.first, "newalloc", intToHex(new_page_addr), '\n');

        insert(process_id, pte_address, new_page_addr, pt_level, virt_address);
        result_pte = get_pte(process_id, pte_address, pt_level);
        // oboviously not entry would be there, hence explicitly return "false" as we just created this entry
        return {false, result_pte.second};
    }  

    Page getpage(int process_id, uint64_t pte_address, int pt_level)
    {
        if(process_id < 0)
        {
            pagetable_logger.log("Error: invalid process/cpu id for getting a page", "process_id", process_id, "addr", intToHex(pte_address), "pt_level", pt_level, '\n');
            exit(-1);
        }
        auto found_process = process_to_pagetable_levels_tracker.find(process_id);
        if(found_process == process_to_pagetable_levels_tracker.end())
        {
            pagetable_logger.log("Error: process/cpu id not found for getting a page", "process_id", process_id, "addr", intToHex(pte_address), "pt_level", pt_level, '\n');
            exit(-1);
        }
        return found_process->second.list_pages_tables_levels[pt_level].list_pages[pte_address >> LOG2_PAGE_SIZE];
    }

    // number of valid PTE and which pte are valid
    pair<int, uint8_t> get_cacheblock_usage(int process_id, uint64_t pte_address, int pt_level, string caller="")
    {
        int usage = 0;
        Page page = getpage(process_id, pte_address, pt_level);
        int cache_block_id = (pte_address >> LOG2_BLOCK_SIZE) & 0x3F; // 6-bit cache block id within page

        if(page.list_cacheblocks.find(cache_block_id) == page.list_cacheblocks.end())
        {
            pagetable_logger.log( "Error: cacheblock not found for getting usage", "cpu", process_id, "page", intToHex(page.page_number), "addr", intToHex(pte_address), "cb", cache_block_id, "pt_level", pt_level, caller, '\n');
            exit(-1);
        }

        uint8_t valid_bits = 0;

        CacheBlock cache_block = page.list_cacheblocks[cache_block_id];
        for(int i=0; i<8; i++)
        {
            auto pte = cache_block.get_pte(i);
            if(pte.first)
            {
                usage++;
                valid_bits |= 1 << i;
            }
        }

        // pagetable_logger.log( "CacheBlockUsage", "cpu", process_id, "addr", intToHex(pte_address), "cb", cache_block_id, "pt_level", pt_level, "usage", usage, "valid_bits", bitset<8>(valid_bits), '\n');
        return {usage, valid_bits};
    }

    // number of valid PTE and which pte are valid
    pair<int, vector<pair<bool, PTEHolder>>> get_cacheblock_data(int process_id, uint64_t pte_address, int pt_level, string caller="")
    {
        int usage = 0;
        Page page = getpage(process_id, pte_address, pt_level);
        int cache_block_id = (pte_address >> LOG2_BLOCK_SIZE) & 0x3F; // 6-bit cache block id within page

        if(page.list_cacheblocks.find(cache_block_id) == page.list_cacheblocks.end())
        {
            pagetable_logger.log( "Error: cacheblock not found for getting usage", "cpu", process_id, "page", intToHex(page.page_number), "addr", intToHex(pte_address), "cb", cache_block_id, "pt_level", pt_level, caller, '\n');
            exit(-1);
        }

        vector<pair<bool, PTEHolder>> pte_list(8);
        CacheBlock cache_block = page.list_cacheblocks[cache_block_id];
        for(int i=0; i<8; i++)
        {
            auto pte = cache_block.get_pte(i);
            if(pte.first)
            {
                usage++;
            }
            pte_list[i] = pte;
        }

        // pagetable_logger.log( "CacheBlockUsage", "cpu", process_id, "addr", intToHex(pte_address), "cb", cache_block_id, "pt_level", pt_level, "usage", usage, "valid_bits", bitset<8>(valid_bits), '\n');
        return {usage, pte_list};
    }

    // full ??
    bool is_translation_block_full(int process_id, uint64_t pte_address, int pt_level, string caller="")
    {
        pair<int, uint8_t> ret = get_cacheblock_usage( process_id,  pte_address,  pt_level);
        return ret.first == 8;
    }

    
    void printStat()
    {
        int pages_by_level[vmem.pt_levels+1] = {0};
        int cacheblocks_by_level[vmem.pt_levels+1] = {0};
        vector<int> cacheBlockOccupancy(9, 0);
        vector<int> cacheBlockOccupancyByLevels[vmem.pt_levels+1];

        for(int i=0; i<= vmem.pt_levels; i++)
            cacheBlockOccupancyByLevels[i] = vector<int>(9,0);

        for(auto entry: process_to_pagetable_levels_tracker)
        {
            int process_id = entry.first;
            PageTableLevelTracker& page_table_level_tracker = entry.second;
            for(int level=1; level<= vmem.pt_levels; level++)
            {
                pages_by_level[level] += page_table_level_tracker.list_pages_tables_levels[level].list_pages.size();
                
                for(auto page_entry: page_table_level_tracker.list_pages_tables_levels[level].list_pages)
                {
                    int page_number = page_entry.first;
                    Page& page = page_entry.second;

                    cacheblocks_by_level[level] += page.list_cacheblocks.size();

                    for(auto cacheblock_entry: page.list_cacheblocks)
                    {
                        int cache_block_id = cacheblock_entry.first;
                        CacheBlock& cache_block = cacheblock_entry.second;
                        
                        int occupancy = 0;
                        for(int i=0; i<8; i++)
                        {
                            auto pte = cache_block.get_pte(i);
                            if(pte.first)
                            {
                                occupancy++;
                            }
                        }

                        cacheBlockOccupancy[occupancy]++;
                        cacheBlockOccupancyByLevels[level][occupancy]++;
                    }
                }
            }
        }

        cout << setw(9) << "Level," << setw(9) << "Pages," << setw(9) << "CacheBlocks" << '\n';
        for(int i=0; i< vmem.pt_levels; i++)
        {
            cout << setw(7) << (i+1) <<","<< setw(7)<< pages_by_level[i+1] <<","<< setw(7) << cacheblocks_by_level[i+1] << '\n';
        }

        cout << setw(9) << "count-1," << setw(9) << "count-2," << setw(9) << "count-3," << setw(9) << "count-4," << setw(9) << "count-5," << setw(9) << "count-6," << setw(9) << "count-7," << setw(9) << "count-8" << '\n';
        for(int i=1; i< 9; i++)
            cout << setw(7) << cacheBlockOccupancy[i] << ", ";
        cout << '\n';
        cout << '\n';

        cout << setw(9) << "level," << setw(9) << "count-1," << setw(9) << "count-2," << setw(9) << "count-3," << setw(9) << "count-4," << setw(9) << "count-5," << setw(9) << "count-6," << setw(9) << "count-7," << setw(9) << "count-8," << '\n';
        for(int level=1; level<= vmem.pt_levels; level++)
        {
            cout <<setw(7) << level << ",";
            for(int i=1; i< 9; i++)
                cout << setw(7) << cacheBlockOccupancyByLevels[level][i] << ",";
            cout << '\n';
        }
    }

    void printTree()
    {
        for(auto entry: process_to_pagetable_levels_tracker)
        {
            int process_id = entry.first;
            PageTableLevelTracker& page_table_level_tracker = entry.second;
            for(int level=1; level<= vmem.pt_levels; level++)
            {
                for(auto page_entry: page_table_level_tracker.list_pages_tables_levels[level].list_pages)
                {
                    int page_number = page_entry.first;
                    Page& page = page_entry.second;
                    for(auto cacheblock_entry: page.list_cacheblocks)
                    {
                        int cache_block_id = cacheblock_entry.first;
                        CacheBlock& cache_block = cacheblock_entry.second;
                        for(int i=0; i<8; i++)
                        {
                            auto pte = cache_block.get_pte(i);
                            if(pte.first)
                                pagetable_logger.log("cpu", process_id, "level", level, "Page", intToHex(page_number), "CB", cache_block_id, "PTE", i, intToHex(pte.second.page_address), '\n');
                        }
                    }
                }
            }
        }
    }
};

#endif // PAGE_TABLE_H