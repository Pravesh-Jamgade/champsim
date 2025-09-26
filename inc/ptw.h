#ifndef PTW_H
#define PTW_H

#include <list>
#include <map>
#include <optional>
#include <string>

#include "delay_queue.hpp"
#include "memory_class.h"
#include "operable.h"

#include "DataModel.h"
#include "cache.h"
#include "logger.h"

class PagingStructureCache
{
  struct block_t {
    bool valid = false;
    uint64_t address;
    uint64_t data;
    uint32_t lru = std::numeric_limits<uint32_t>::max() >> 1;
    int thread_id = -1;
  };

  const std::string NAME;
  const uint32_t NUM_SET, NUM_WAY;
  std::vector<block_t> block{NUM_SET * NUM_WAY};

public:
  const std::size_t level;
  PagingStructureCache(std::string v1, uint8_t v2, uint32_t v3, uint32_t v4) : NAME(v1), NUM_SET(v3), NUM_WAY(v4), level(v2) {}

  std::optional<uint64_t> check_hit(uint64_t address, uint64_t vaddr, int thread_id);
  void fill_cache(uint64_t next_level_paddr, uint64_t vaddr, int thread_id);

  void invalidate(int thread_id)
  {
    for(auto &cb: block)
    {
      if(thread_id == cb.thread_id)
      {
        cb.address = 0;
        cb.data = 0;
        cb.valid = 0;
      }
    }
  }
};

typedef struct Track
{
  bool stop = false;
  uint64_t readmiss_address;
  uint64_t readmiss_v_address;
};

class PageTableWalker : public champsim::operable, public MemoryRequestConsumer, public MemoryRequestProducer
{
public:

  enum PageFeature
  {
    PTW_Freq=0,
    PTW_Cost,
    PageFeature_end
  };

  logger debugLog;
  logger dlog;
  Track track;

  CACHE* llcObject;

  const std::string NAME;
  const uint32_t cpu;
  const uint32_t MSHR_SIZE, MAX_READ, MAX_FILL;

  champsim::delay_queue<PACKET> RQ;
  // champsim::list<PACKET> RQ;

  std::list<PACKET> MSHR;

  uint64_t total_miss_latency = 0;

  PagingStructureCache PSCL5, PSCL4, PSCL3, PSCL2;

  vector<uint64_t> CR3_addr;
  uint64_t POMTLB_baseaddr;
  vector<uint64_t> asid;

  // std::map<std::pair<uint64_t, std::size_t>, uint64_t> page_table;
  list<PagingStructureCache*> pscl_array;

  // usercode
  PTWDataModel* ptw_datamodel;
  vector<int> fill_counters;

  PageTableWalker(std::string v1, uint32_t cpu, unsigned fill_level, uint32_t v2, uint32_t v3, uint32_t v4, uint32_t v5, uint32_t v6, uint32_t v7, uint32_t v8,
                  uint32_t v9, uint32_t v10, uint32_t v11, uint32_t v12, uint32_t v13, unsigned latency, MemoryRequestConsumer* ll, CACHE* llc);

  ~PageTableWalker()
  {
    for(int i=0; i< fill_counters.size(); i++)
    {
      cout << "Fill count level " << i << ", " << fill_counters[i] << '\n';
    }
    ptw_datamodel->print_stats();
  }
  // functions
  int add_rq(PACKET* packet) override;
  int add_wq(PACKET* packet) override { assert(0); }
  int add_pq(PACKET* packet) override { assert(0); }

  void return_data(PACKET* packet) override;
  void operate() override;

  void handle_read();
  void handle_fill();

  uint32_t get_occupancy(uint8_t queue_type, uint64_t address) override;
  uint32_t get_size(uint8_t queue_type, uint64_t address) override;

  uint64_t get_shamt(uint8_t pt_level);
  void print_deadlock() override;

  void* getObject(){return this;}
  void victima_update(uint64_t addr, int signal, int hit_where=-1);
  void _overwrite();
  void _context_switch(int thread_id) 
  {
    for(auto pscl: pscl_array)
    {
      pscl->invalidate(thread_id);
    }
  }

  uint64_t get_pomtlb_baseaddr() {
    return POMTLB_baseaddr;
  } 

};

#endif
