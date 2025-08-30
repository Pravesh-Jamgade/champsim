#ifndef MEMORY_CLASS_H
#define MEMORY_CLASS_H

#include <limits>
#include <map>

#include "block.h"
#include "pagetable.h"

// CACHE ACCESS TYPE
#define LOAD 0
#define RFO 1
#define PREFETCH 2
#define WRITEBACK 3
#define TRANSLATION 4
#define NUM_TYPES 5

#define READ_HIT 0
#define WRITEBACK_HIT 1

class VPN
{
  public:
  bool valid = false;
  uint64_t vp = 0;
  uint64_t pp = 0;
};

// CACHE BLOCK
class BLOCK
{
public:
  bool valid = false, prefetch = false, dirty = false;

  uint64_t address = 0, v_address = 0, tag = 0, data = 0, ip = 0, cpu = 0, instr_id = 0;

  // replacement state
  uint32_t lru = std::numeric_limits<uint32_t>::max() >> 1;

  int came_from_request = NUM_TYPES; // default: invalid block

  uint32_t m_used = 0; // 8 entries of 8B each
  bool victima_block = 0;
  std::map<uint64_t, uint64_t> vp_2_pp_map;
 
  CacheBlock cacheBlock;
  DataType dtype = DataType::INVALID;

  // for tlb block, each PTE is 8Byte, so we have 8 entries in 64B block
  void updateUsage(uint32_t offset){
    uint32_t mask = offset;
    m_used |= 1 << mask;
  }

  // use only for TLB blocks
  int getUsage()
  {
    return __builtin_popcount(m_used);
  }
  int thread_id = -1;
};

class MemoryRequestConsumer
{
public:
  /*
   * add_*q() return values:
   *
   * -2 : queue full
   * -1 : packet value forwarded, returned
   * 0  : packet merged
   * >0 : new queue occupancy
   *
   */

  uint64_t numPPages = 0;
  bool* procPageAccess;

  const unsigned fill_level;
  virtual int add_rq(PACKET* packet) = 0;
  virtual int add_wq(PACKET* packet) = 0;
  virtual int add_pq(PACKET* packet) = 0;
  virtual uint32_t get_occupancy(uint8_t queue_type, uint64_t address) = 0;
  virtual uint32_t get_size(uint8_t queue_type, uint64_t address) = 0;

  virtual void* getObject() = 0;

  explicit MemoryRequestConsumer(unsigned fill_level) : fill_level(fill_level) {}
};

class MemoryRequestProducer
{
public:
  MemoryRequestConsumer* lower_level;
  virtual void return_data(PACKET* packet) = 0;
  virtual void* getObject() = 0;
protected:
  MemoryRequestProducer() {}
  explicit MemoryRequestProducer(MemoryRequestConsumer* ll) : lower_level(ll) {}
};

#endif
