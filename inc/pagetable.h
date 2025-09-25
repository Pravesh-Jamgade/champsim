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
    public:
    // key: byte offset within the base page (you may choose to store a line index instead)
    // val: allocated physical page base
    // tuple {addr>PAGE_SIZE, addr>>shtamt(level-1), thread_id}
    std::unordered_map<tuple<uint64_t, uint64_t, int>, uint64_t> pte;

    void map_entry(uint64_t ptekey, uint64_t allocated_page, int level)
    {
       tuple<int, uint64_t, uint64_t> key = make_tuple(level, ptekey>>(vmem.shamt(level-1)), level==0?-1:ptekey%8);
        // Update if exists
        pte.insert_or_assign(key, allocated_page);
        // pagetable_logger.log("INSERTED","ptekey",intToHex(ptekey), "ptevalue",intToHex(allocated_page), '\n');
    }

    int getUsage() const { return pte.size(); }
};

class Page
{
    public:
    bool page_table_page = 0;
    int page_table_level = -1;
    uint64_t base_addr = 0;
    // key: cache-block index within the page (0..kBlocksPerPage-1)
    std::unordered_map<uint64_t, CacheBlock> blocks;

    Page(){}
    Page(uint64_t base)
    {
        this->base_addr = base;
    }

    void set_page_type(int level)
    {
        page_table_page = level != 0;
        page_table_level = level;
    }

    void map_entry(uint64_t offset_within_base_addr, uint64_t ptekey, uint64_t allocated_page, int level)
    {   
        // offset must be within the page
        const uint64_t cbid = block_index(offset_within_base_addr);
        auto &blk = blocks[cbid]; // creates on demand
        blk.map_entry(ptekey, allocated_page, level);
    }

    bool empty() const { return blocks.empty(); }

    void print_stat()
    {
        cout << intToHex(base_addr) << std::dec << ", cache_blocks: " << blocks.size() << ", level, " << page_table_level << '\n';

        for (auto entry : blocks)
        {
            cout << "block: " << entry.first << ", ptes: " << entry.second.getUsage() << '\n';
        }
    }

    void print_stat_detail()
    {
        cout << "page," << intToHex(base_addr) << std::dec << ", cache_blocks: " << blocks.size() << ", level, " << page_table_level << '\n';

        for (auto cacheBlockEntry : blocks)
        {
            for(auto pteEntry: cacheBlockEntry.second.pte)
            {
                cout << "cb, " << cacheBlockEntry.first << ", pte, [page=" << intToHex(get<0>(pteEntry.first)) << ", addrTrace=" << intToHex(get<1>(pteEntry.first)) << ", thread_id=" << get<2>(pteEntry.first) << "]" << ", "<< intToHex(page_align(pteEntry.second)) << '\n';
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

    // Insert a mapping
    void insert(uint64_t complete_address, uint64_t ptekey, uint64_t new_allocate_addr, uint8_t level)
    {
        // Align addresses to page boundaries
        const uint64_t base_page = page_align(complete_address);
        const uint64_t allocated_page = page_align(new_allocate_addr);

        // Ensure both pages exist
        auto &basePg = pages.try_emplace(base_page, Page(base_page)).first->second;
        auto &allocPg = pages.try_emplace(allocated_page, Page(allocated_page)).first->second; // reserve allocated mapping page if you track both

        // assign level for base page
        basePg.set_page_type(level);

        // map new allocated page to ptekey within cache block of base page
        basePg.map_entry(complete_address, ptekey, allocated_page, level);

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
    uint64_t lookupEntry(int cpu, uint64_t phys_addr, uint64_t virt_addr, int level, uint64_t existingData)
    {
        if(level == 0) return 0;

        const uint64_t phy_base = page_align(phys_addr);
        uint64_t use_addr = (level ==1 ) ? virt_addr : phys_addr;
        tuple<int, uint64_t, uint64_t> search_key = make_tuple(level, use_addr>>(vmem.shamt(level-1)), level==0?-1:use_addr%8);

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
      
        pagetable_logger.log("PTE not found: ", "addr", intToHex(phys_addr), "vaddr", intToHex(virt_addr), "base_page",intToHex(phy_base), "cache_block",cbid, "ptekey: ",intToHex(get<0>(search_key)), intToHex(get<1>(search_key)), intToHex(get<2>(search_key)), "level", (int)level, "exitingData", intToHex(existingData),'\n');
        exit(-1);
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