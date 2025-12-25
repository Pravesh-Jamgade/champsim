#ifndef VMEM_H
#define VMEM_H

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <tuple>
#include <vector>

// reserve 1MB of space
#define VMEM_RESERVE_CAPACITY 1048576

#define PRE_ALLOC_LIMIT 10

#define PTE_BYTES 8

class BuddyAllocator
{
public:
  BuddyAllocator(uint64_t base_addr, uint64_t total_bytes, uint64_t page_size);

  uint64_t allocate(uint64_t num_pages = 1);
  void free_block(uint64_t addr, uint64_t num_pages);

  size_t free_page_count() const;

private:
  uint64_t base_addr;
  uint64_t page_size;
  uint32_t max_order;
  std::vector<std::set<uint64_t>> free_lists;

  uint32_t ceil_log2(uint64_t value) const;
  uint32_t exact_log2(uint64_t value) const;
  void insert_block(uint64_t addr, uint32_t order);
};

class VirtualMemory
{
private:
  std::map<std::pair<uint32_t, uint64_t>, uint64_t> vpage_to_ppage_map;
  std::map<std::tuple<uint32_t, uint64_t, uint32_t>, uint64_t> page_table;

  BuddyAllocator allocator;

  uint64_t allocate_pagetable_page();

public:
  const uint64_t minor_fault_penalty;
  uint32_t pt_levels;
  const uint32_t page_size; // Size of a PTE page
  std::deque<uint64_t> pom_tlb_pages;

  // capacity and pg_size are measured in bytes, and capacity must be a multiple
  // of pg_size
  VirtualMemory(uint64_t capacity, uint64_t pg_size, uint32_t page_table_levels, uint64_t random_seed, uint64_t minor_fault_penalty);
  uint64_t shamt(uint32_t level) const;
  uint64_t get_offset(uint64_t vaddr, uint32_t level) const;
  std::pair<uint64_t, bool> va_to_pa(uint32_t cpu_num, uint64_t vaddr);
  std::pair<uint64_t, bool> get_pte_pa(uint32_t cpu_num, uint64_t vaddr, uint32_t level);
  uint64_t func_allocate_page();

  size_t free_page_count() const;
};

#endif
