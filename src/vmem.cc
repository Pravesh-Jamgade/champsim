#include "vmem.h"

#include <algorithm>
#include <cassert>
#include <iostream>

#include "champsim.h"
#include "util.h"

// representing 1MB of memory reserved for POM-TLB 256*4KB=1MB, stores 2^17 PTEs
#define POM_TLB_PAGES 256

BuddyAllocator::BuddyAllocator(uint64_t base_addr, uint64_t total_bytes, uint64_t page_sz)
    : base_addr(base_addr), page_size(page_sz)
{
  assert(page_size != 0);
  assert(total_bytes % page_size == 0);

  uint64_t total_pages = total_bytes / page_size;
  assert(total_pages > 0);

  max_order = 0;
  while ((1ULL << max_order) <= total_pages) {
    max_order++;
  }
  if (max_order > 0)
    max_order--;

  free_lists.resize(max_order + 1);

  uint64_t pages_remaining = total_pages;
  uint64_t offset_pages = 0;
  while (pages_remaining > 0) {
    uint32_t order = 0;
    while (((offset_pages % (1ULL << (order + 1))) == 0) && ((1ULL << (order + 1)) <= pages_remaining)) {
      order++;
    }

    insert_block(base_addr + offset_pages * page_size, order);
    offset_pages += 1ULL << order;
    pages_remaining -= 1ULL << order;
  }
}

uint32_t BuddyAllocator::ceil_log2(uint64_t value) const
{
  uint32_t order = 0;
  uint64_t size = 1;
  while (size < value) {
    size <<= 1;
    order++;
  }
  return order;
}

uint32_t BuddyAllocator::exact_log2(uint64_t value) const
{
  assert((value & (value - 1)) == 0);
  uint32_t order = 0;
  while ((1ULL << order) < value)
    order++;
  return order;
}

void BuddyAllocator::insert_block(uint64_t addr, uint32_t order)
{
  assert(order < free_lists.size());
  free_lists[order].insert(addr);
}

uint64_t BuddyAllocator::allocate(uint64_t num_pages)
{
  assert(num_pages > 0);
  uint32_t order = ceil_log2(num_pages);

  for (uint32_t current = order; current <= max_order; current++) {
    if (!free_lists[current].empty()) {
      uint64_t block_addr = *free_lists[current].begin();
      free_lists[current].erase(free_lists[current].begin());

      while (current > order) {
        current--;
        uint64_t buddy_addr = block_addr + (1ULL << current) * page_size;
        insert_block(buddy_addr, current);
      }

      return block_addr;
    }
  }

  std::cerr << "Out of physical memory in BuddyAllocator" << std::endl;
  std::abort();
}

void BuddyAllocator::free_block(uint64_t addr, uint64_t num_pages)
{
  assert(num_pages > 0 && (num_pages & (num_pages - 1)) == 0);
  uint32_t order = exact_log2(num_pages);

  while (order < max_order) {
    uint64_t block_size_bytes = (1ULL << order) * page_size;
    uint64_t relative = (addr - base_addr) / block_size_bytes;
    uint64_t buddy_addr = base_addr + ((relative ^ 1ULL) * block_size_bytes);

    auto it = free_lists[order].find(buddy_addr);
    if (it == std::end(free_lists[order]))
      break;

    free_lists[order].erase(it);
    addr = std::min(addr, buddy_addr);
    order++;
  }

  insert_block(addr, order);
}

size_t BuddyAllocator::free_page_count() const
{
  size_t total = 0;
  for (uint32_t order = 0; order < free_lists.size(); order++) {
    total += free_lists[order].size() * (1ULL << order);
  }
  return total;
}

VirtualMemory::VirtualMemory(uint64_t capacity, uint64_t pg_size, uint32_t page_table_levels, uint64_t random_seed, uint64_t minor_fault_penalty)
    : vpage_to_ppage_map{}, page_table{}, allocator(VMEM_RESERVE_CAPACITY, capacity - VMEM_RESERVE_CAPACITY, PAGE_SIZE), minor_fault_penalty(minor_fault_penalty), pt_levels(page_table_levels), page_size(pg_size)
{
  assert(capacity % PAGE_SIZE == 0);
  assert(pg_size == (1ul << lg2(pg_size)) && pg_size > 1024);
  assert(pg_size % PAGE_SIZE == 0);

  (void)random_seed; // randomization handled by buddy allocator ordering

  // we need contiguous pages for POM-TLB
  uint64_t pom_base = allocator.allocate(POM_TLB_PAGES);
  for (uint64_t i = 0; i < POM_TLB_PAGES; i++)
    pom_tlb_pages.push_back(pom_base + i * PAGE_SIZE);
}

uint64_t VirtualMemory::shamt(uint32_t level) const { return LOG2_PAGE_SIZE + lg2(page_size / PTE_BYTES) * (level); }

uint64_t VirtualMemory::get_offset(uint64_t vaddr, uint32_t level) const { return (vaddr >> shamt(level)) & bitmask(lg2(page_size / PTE_BYTES)); }

// original
std::pair<uint64_t, bool> VirtualMemory::va_to_pa(uint32_t cpu_num, uint64_t vaddr)
{
  auto [ppage, inserted] = vpage_to_ppage_map.try_emplace(std::pair{cpu_num, vaddr >> LOG2_PAGE_SIZE}, uint64_t{0});
  if (inserted)
    ppage->second = allocator.allocate();

  return {splice_bits(ppage->second, vaddr, LOG2_PAGE_SIZE), inserted};
}

std::pair<uint64_t, bool> VirtualMemory::get_pte_pa(uint32_t cpu_num, uint64_t vaddr, uint32_t level)
{
  std::tuple key{cpu_num, vaddr >> shamt(level-1), level};
  auto [ppage, inserted] = page_table.try_emplace(key, uint64_t{0});
  if (inserted)
    ppage->second = allocate_pagetable_page();

  return {splice_bits(ppage->second, get_offset(vaddr, level-1) * PTE_BYTES, lg2(page_size)), inserted};
}

uint64_t VirtualMemory::func_allocate_page()
{
  return allocator.allocate();
}

uint64_t VirtualMemory::allocate_pagetable_page()
{
  assert(page_size % PAGE_SIZE == 0);
  uint64_t num_pages = page_size / PAGE_SIZE;
  return allocator.allocate(num_pages);
}

size_t VirtualMemory::free_page_count() const
{
  return allocator.free_page_count();
}
