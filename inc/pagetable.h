#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include <cstdint>
#include <unordered_map>
#include <optional>
#include <cassert>

#include "champsim_constants.h"

// Compile-time sanity
static_assert((PAGE_SIZE & (PAGE_SIZE - 1)) == 0, "PAGE_SIZE must be power of two");
static_assert((BLOCK_SIZE & (BLOCK_SIZE - 1)) == 0, "BLOCK_SIZE must be power of two");
constexpr uint64_t kBlocksPerPage = PAGE_SIZE / BLOCK_SIZE;

// Utilities
inline constexpr uint64_t page_align(uint64_t addr) { return addr & ~(PAGE_SIZE - 1ULL); }
inline constexpr uint64_t block_index(uint64_t off) { return (off / BLOCK_SIZE) % kBlocksPerPage; }

struct CacheBlock
{
    // key: byte offset within the base page (you may choose to store a line index instead)
    // val: allocated physical page base
    std::unordered_map<uint64_t, uint64_t> pte;

    void map_entry(uint64_t offset_within_base_addr, uint64_t allocated_addr)
    {
        const uint64_t allocated_page = page_align(allocated_addr);
        // Update if exists
        pte.insert_or_assign(offset_within_base_addr, allocated_page);
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

struct Page
{
    bool data_or_pt_page = 0;
    int page_table_level = 0;
    uint64_t base_addr = 0;
    // key: cache-block index within the page (0..kBlocksPerPage-1)
    std::unordered_map<uint64_t, CacheBlock> blocks;

    explicit Page(uint64_t base = 0) : base_addr(page_align(base)) {}

    void set_page_type(int level)
    {
        data_or_pt_page = level != 0;
        page_table_level = level;
    }

    void map_entry(uint64_t offset_within_base_addr, uint64_t allocated_addr)
    {
        // offset must be within the page
        const uint64_t cbid = block_index(offset_within_base_addr);
        auto &blk = blocks[cbid]; // creates on demand
        blk.map_entry(offset_within_base_addr, allocated_addr);
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
        cout << std::hex << base_addr << std::dec << ", cache_blocks: " << blocks.size() << ", level, " << page_table_level << '\n';

        for (auto entry : blocks)
        {
            cout << "block: " << entry.first << ", ptes: " << blocks.size() << '\n';
        }
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
    void insert(uint64_t base_page_table_addr, uint64_t offset_within_base_addr, uint64_t new_allocate_addr, uint8_t level)
    {
        const uint64_t base_page = page_align(base_page_table_addr);
        const uint64_t allocated_page = page_align(new_allocate_addr);

        // Ensure both pages exist
        auto &basePg = pages.try_emplace(base_page, Page(base_page)).first->second;
        auto &allocPg = pages.try_emplace(allocated_page, Page(allocated_page)).first->second; // reserve allocated mapping page if you track both

        allocPg.set_page_type(level);

        basePg.map_entry(offset_within_base_addr, allocated_page);

        ptw_level_pages[(int)level]++;
    }

    // non-const overload
    CacheBlock *lookup(uint64_t phys_addr)
    {
        const uint64_t page_base = phys_addr & ~(PAGE_SIZE - 1ULL);
        auto pit = pages.find(page_base);
        if (pit == pages.end())
            return nullptr;

        const uint64_t off = phys_addr - page_base;
        const uint64_t cbid = block_index(off);

        auto bit = pit->second.blocks.find(cbid);
        if (bit == pit->second.blocks.end())
            return nullptr;
        return &bit->second; // type: CacheBlock*
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

    void print_stat(int thread)
    {
        // total_pages_at_each_level + cr3_allocated_page
        cout << "total_pages_at_each_level + cr3_allocated_page: " << pages.size() << '\n';
        for (int i = 0; i < ptw_level_pages.size(); i++)
        {
            cout << "Thread " << thread << " Pagetable level " << i << ": " << ptw_level_pages[i] << '\n';
        }
        // for(auto page: pages)
        // {
        //     // if(page.second.data_or_pt_page)
        //         page.second.print_stat();
        // }
    }
};

#endif // PAGE_TABLE_H