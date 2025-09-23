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

class CacheBlock
{
    public:
    // key: byte offset within the base page (you may choose to store a line index instead)
    // val: allocated physical page base
    std::unordered_map<uint64_t, uint64_t> pte;

    void map_entry(uint64_t ptekey, uint64_t allocated_page)
    {
        // Represent address of 8byte location
        ptekey = (ptekey >> 12) & (~7);
        // Update if exists
        pte.insert_or_assign(ptekey, allocated_page);
        
        // pagetable_logger.log("INSERTED","ptekey",intToHex(ptekey), "ptevalue",intToHex(allocated_page), '\n');
    }

    bool erase_offset(uint64_t offset_within_base_addr)
    {
        return pte.erase(offset_within_base_addr) != 0;
    }

    bool empty() const { return pte.empty(); }

    int getUsage()
    {
        return pte.size();
    }
};

class Page
{
    public:
    bool page_table_page = 0;
    int page_table_level = -1;
    uint64_t base_addr = 0;
    uint64_t base_full_addr = 0;
    // key: cache-block index within the page (0..kBlocksPerPage-1)
    std::unordered_map<uint64_t, CacheBlock> blocks;

    Page(){}
    Page(uint64_t base, uint64_t base_full_addr)
    {
        this->base_addr = base;
        this->base_full_addr = base_full_addr;
    }

    void set_page_type(int level)
    {
        page_table_page = level != 0;
        page_table_level = level;
    }

    void map_entry(uint64_t offset_within_base_addr, uint64_t ptekey, uint64_t allocated_page)
    {   
        
        // missing page
        if(8469217280 == allocated_page)
            cout << "missing page in pagetable as PTE, key:" << intToHex(ptekey) << ", value:" << intToHex(allocated_page) << '\n';
        
        // offset must be within the page
        const uint64_t cbid = block_index(offset_within_base_addr);

        if(page_align(offset_within_base_addr) == 3750301696 && cbid == 43)
            cout << "page, " << intToHex(page_align(offset_within_base_addr))<< ", key, " << intToHex(ptekey) << ", value, " << intToHex(allocated_page) << '\n';

        auto &blk = blocks[cbid]; // creates on demand
        blk.map_entry(ptekey, allocated_page);
    }

    // Optional: clean up empty blocks
    bool erase_offset(uint64_t offset_within_base_addr)
    {
        const uint64_t cbid = block_index(offset_within_base_addr);
        auto it = blocks.find(cbid);
        if (it == blocks.end())
            return false;
        bool removed = it->second.erase_offset(offset_within_base_addr);
        if (removed && it->second.empty())
            blocks.erase(it);
        return removed;
    }

    bool empty() const { return blocks.empty(); }

    void print_stat()
    {
        cout << intToHex(page_align(base_full_addr)) << std::dec << ", cache_blocks: " << blocks.size() << ", level, " << page_table_level << '\n';

        for (auto entry : blocks)
        {
            cout << "block: " << entry.first << ", ptes: " << entry.second.getUsage() << '\n';
        }
    }

    void print_stat_detail()
    {
        cout << "page," << intToHex(page_align(base_full_addr)) << std::dec << ", cache_blocks: " << blocks.size() << ", level, " << page_table_level << '\n';

        for (auto cacheBlockEntry : blocks)
        {
            for(auto pteEntry: cacheBlockEntry.second.pte)
            {
                cout << "cb, " << cacheBlockEntry.first << ", pte, " << intToHex(pteEntry.first) << ", "<< intToHex(page_align(pteEntry.second)) << '\n';
            }
            cout << '\n';
        }
    }

    vector<int> get_block_level_occupancy_histogram()
    {
        vector<int> hist(9,0);
        
        for(int i=0; i< 9; i++) hist[i] = 0;

        for (auto entry : blocks)
        {
            auto temp = entry.second.getUsage();
            hist[temp]++;
        }
        return hist;
    }
};

class PageTable
{
public:
    // key: page base address
    std::unordered_map<uint64_t, Page> pages;

    vector<int> ptw_level_pages;

    PageTable()
    {
        ptw_level_pages.resize(5, 0);
    }

    // Insert/overwrite a mapping: (base page table addr, offset within that page) -> allocated physical page
    void insert(uint64_t base_page_table_addr, uint64_t offset_within_base_addr, uint64_t ptekey, uint64_t new_allocate_addr, uint8_t level)
    {
        const uint64_t base_page = page_align(offset_within_base_addr);
        const uint64_t allocated_page = page_align(new_allocate_addr);

        // Ensure both pages exist
        auto &basePg = pages.try_emplace(base_page, Page(base_page,base_page_table_addr)).first->second;
        auto &allocPg = pages.try_emplace(allocated_page, Page(allocated_page,new_allocate_addr)).first->second; // reserve allocated mapping page if you track both

        // since it is the next-level page and if level==1 then its a data page
        basePg.set_page_type(level);

        basePg.map_entry(offset_within_base_addr, ptekey, allocated_page);

        // we counting how many PTE are inserted here. Implies how many faults happened at Next-level and inserted PTE on current level here
        ptw_level_pages[(int)level]++;
    }

    // non-const overload
    CacheBlock *lookup(uint64_t phys_addr)
    {
        const uint64_t page_base = page_align(phys_addr);
        auto pit = pages.find(page_base);
        if (pit == pages.end())
        {
            pagetable_logger.log("[Tester] PageTable lookup failed: Page not found", intToHex(page_base), '\n');
            return nullptr;
        }
            
        const uint64_t cbid = block_index(phys_addr);

        auto bit = pit->second.blocks.find(cbid);
        if (bit == pit->second.blocks.end())
        {
            pagetable_logger.log("[Tester] Cacheblock lookup failed: Cache block not found", "addr", intToHex(phys_addr) , "page", intToHex(page_base), "cb", cbid, '\n');
            return nullptr;
        }
            
        return &bit->second; // type: CacheBlock*
    }

    // non-const overload
    uint64_t lookupEntry(uint64_t phys_addr, uint64_t virt_addr, uint8_t level, uint64_t existingData)
    {
        if(level == 0) return 0;

        const uint64_t phy_base = page_align(phys_addr);
        uint64_t search_key = phys_addr;// (level ==1 ) ? virt_addr : phys_addr;
        search_key = (search_key >> 12) & (~7);

        auto pit = pages.find(phy_base);
        if (pit == pages.end())
        {
            for(auto curPage: pages)
                curPage.second.print_stat_detail();
            pagetable_logger.log("PageTable lookup failed: Page not found", "addr", intToHex(phys_addr), "page", intToHex(phy_base), "level", level, '\n');
            exit(-1);
        }
            
        const uint64_t cbid = block_index(phys_addr);

        auto bit = pit->second.blocks.find(cbid);
        if (bit == pit->second.blocks.end())
        {
            for(auto curPage: pages)
                curPage.second.print_stat_detail();
      
            pagetable_logger.log("Cacheblock lookup failed: Cache block not found", "addr", intToHex(phys_addr) , "page", intToHex(phy_base), "cb", cbid, "level", (int)level, "exitingData", intToHex(existingData), '\n');
           
            for(auto pte: vmem.get_pagetable())
            {
                pagetable_logger.log(get<0>(pte.first), get<1>(pte.first), get<2>(pte.first), "page-next", intToHex(page_align(pte.second)), '\n');
            }

            exit(-1);
        }
            
        CacheBlock* cb = &bit->second; // type: CacheBlock*
        auto pointer = cb->pte.find(search_key);
        if(pointer != cb->pte.end())
        {
            return pointer->second;
        }
        
        for(auto curPage: pages)
            curPage.second.print_stat_detail();
      
        pagetable_logger.log("PTE not found: ", "addr", intToHex(phys_addr), "vaddr", intToHex(virt_addr), "base_page",intToHex(phy_base), "cache_block",cbid, "ptekey",intToHex(search_key), "level", (int)level, "exitingData", intToHex(existingData),'\n');
        exit(-1);
    }

    // Erase mapping at (base page + offset)
    bool erase(uint64_t base_page_table_addr, uint64_t offset_within_base_addr)
    {
        const uint64_t base_page = page_align(base_page_table_addr);
        auto it = pages.find(base_page);
        if (it == pages.end())
            return false;
        bool removed = it->second.erase_offset(offset_within_base_addr);
        if (removed && it->second.empty())
            pages.erase(it);
        return removed;
    }

    // Erase an entire page (e.g., on invalidation)
    bool erase_page(uint64_t any_addr_in_page)
    {
        return pages.erase(page_align(any_addr_in_page)) != 0;
    }

    bool contains_page(uint64_t any_addr_in_page) const
    {
        return pages.find(page_align(any_addr_in_page)) != pages.end();
    }

    size_t size_pages() const { return pages.size(); }

    void print_stat_detail(uint64_t addr)
    {
        pages[page_align(addr)].print_stat_detail();
    }

    void print_stat(int thread)
    {
        // // TODO enable for DEBUG only
        // for(auto page: pages)
        // {
        //     print_stat_detail(page.first);
        // }
        // cout << '\n';
        // for(auto page: pages)
        // {
        //     // if(page.second.page_table_page)
        //         page.second.print_stat();
        // }

        cout << '\n';
        // total_pages_at_each_level + cr3_allocated_page
        cout << "total_pages_at_each_level + cr3_allocated_page: " << pages.size() << '\n';
        for (int i = 0; i < ptw_level_pages.size(); i++)
        {
            cout << "Thread " << thread << " Pagetable level " << i << ": " << ptw_level_pages[i] << '\n';
        }

        map<int,int> accumulate_page_level_occupancy;
        for(int i=0; i< 9; i++) accumulate_page_level_occupancy[i] = 0;

        for(auto page: pages)
        {
            if(page.second.page_table_page == 0)
                continue;
            
            vector<int> accumulate_block_level_occupancy(9,0);

            // occupancy & frequency
            int nonzero_sum = 0;
            vector<int> block_level_histogram = page.second.get_block_level_occupancy_histogram();
            for(int i=0; i< block_level_histogram.size(); i++)
            {
                accumulate_block_level_occupancy[i] += block_level_histogram[i];
                accumulate_page_level_occupancy[i] += block_level_histogram[i];
                nonzero_sum += block_level_histogram[i];
            }

            // // if(nonzero_sum != 0)
            // {
            //     cout << "page: " << intToHex(page_align(page.first)) << ", level, " << page.second.page_table_level;
            //     for(auto printEntry: accumulate_block_level_occupancy)
            //         cout << setw(6) << printEntry << ",";
            //     cout << '\n';
            // }
        }

        cout << "Histogram of occupancy of cache block\n";
        cout << setw(7) << " " << "|";
        for(int i=0; i< accumulate_page_level_occupancy.size(); i++)
            cout << setw(5) << i;
        cout << '\n';

        // Print separator line
        cout << string(7, '-') << "+";
        for(int i=0; i< accumulate_page_level_occupancy.size(); i++)
            cout << setw(5) << "-";
        cout << '\n';

        // Print each row
        cout << setw(7) << " " << "|";
        for(int i=0; i< accumulate_page_level_occupancy.size(); i++)
            cout << setw(5) << accumulate_page_level_occupancy[i] << ',';
        cout << '\n';
    }
};

#endif // PAGE_TABLE_H