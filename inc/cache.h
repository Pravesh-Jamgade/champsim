#ifndef CACHE_H
#define CACHE_H

#include <functional>
#include <list>
#include <string>
#include <vector>

#include "champsim.h"
#include "delay_queue.hpp"
#include "memory_class.h"
#include "ooo_cpu.h"
#include "operable.h"

#include "DataModel.h"

// virtual address space prefetching
#define VA_PREFETCH_TRANSLATION_LATENCY 2

extern std::array<O3_CPU*, NUM_CPUS> ooo_cpu;

class CACHE : public champsim::operable, public MemoryRequestConsumer, public MemoryRequestProducer
{
public:
  //usercode
  CacheDataModel* cacheDataModel;
  bool cache_is[CACHE_ID_END] = {false};

  uint32_t cpu;
  const std::string NAME;
  const uint32_t NUM_SET, NUM_WAY, WQ_SIZE, RQ_SIZE, PQ_SIZE, MSHR_SIZE;
   uint32_t HIT_LATENCY, FILL_LATENCY, OFFSET_BITS, WRITE_LANTENCY;
  std::vector<BLOCK> block{NUM_SET * NUM_WAY};
  const uint32_t MAX_READ, MAX_WRITE;
  uint32_t reads_available_this_cycle, writes_available_this_cycle;
  const bool prefetch_as_load;
  const bool match_offset_bits;
  const bool virtual_prefetch;
  bool ever_seen_data = false;
  const unsigned pref_activate_mask = (1 << static_cast<int>(LOAD)) | (1 << static_cast<int>(PREFETCH));

  // prefetch stats
  uint64_t pf_requested = 0, pf_issued = 0, pf_useful = 0, pf_useless = 0, pf_fill = 0;

  // queues
  champsim::delay_queue<PACKET> RQ{RQ_SIZE, HIT_LATENCY}, // read queue
      PQ{PQ_SIZE, HIT_LATENCY},                           // prefetch queue
      VAPQ{PQ_SIZE, VA_PREFETCH_TRANSLATION_LATENCY},     // virtual address prefetch queue
      WQ{WQ_SIZE, WRITE_LANTENCY},
      TQ{RQ_SIZE, HIT_LATENCY};                           // write queue

  std::list<PACKET> MSHR; // MSHR

  uint64_t sim_access[NUM_CPUS][NUM_TYPES] = {}, sim_hit[NUM_CPUS][NUM_TYPES] = {}, sim_miss[NUM_CPUS][NUM_TYPES] = {}, roi_access[NUM_CPUS][NUM_TYPES] = {},
           roi_hit[NUM_CPUS][NUM_TYPES] = {}, roi_miss[NUM_CPUS][NUM_TYPES] = {};

  uint64_t RQ_ACCESS = 0, RQ_MERGED = 0, RQ_FULL = 0, RQ_TO_CACHE = 0, PQ_ACCESS = 0, PQ_MERGED = 0, PQ_FULL = 0, PQ_TO_CACHE = 0, WQ_ACCESS = 0, WQ_MERGED = 0,
           WQ_FULL = 0, WQ_FORWARD = 0, WQ_TO_CACHE = 0;

  uint64_t total_miss_latency = 0;

  int **prefetch_hit_histo;

  // functions
  int add_rq(PACKET* packet) override;
  int add_wq(PACKET* packet) override;
  int add_pq(PACKET* packet) override;

  void return_data(PACKET* packet) override;
  void operate() override;
  void operate_writes();
  void operate_reads();

  uint32_t get_occupancy(uint8_t queue_type, uint64_t address) override;
  uint32_t get_size(uint8_t queue_type, uint64_t address) override;

  uint32_t get_set(uint64_t address);
  uint32_t get_way(uint64_t address, uint32_t set);

  int invalidate_entry(uint64_t inval_addr);
  int prefetch_line(uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata);
  int prefetch_line(uint64_t ip, uint64_t base_addr, uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata); // deprecated

  void add_mshr(PACKET* packet);
  void va_translate_prefetches();

  void handle_fill();
  void handle_writeback();
  void handle_read();
  void handle_prefetch();

  void readlike_hit(std::size_t set, std::size_t way, PACKET& handle_pkt);
  bool readlike_miss(PACKET& handle_pkt);
  bool filllike_miss(std::size_t set, std::size_t way, PACKET& handle_pkt);

  bool should_activate_prefetcher(int type);

  void print_deadlock() override;

  void* getObject(){return this;}

  void reset_datamodel()
  {
    delete cacheDataModel;
    cacheDataModel = new CacheDataModel(NAME, cpu);
  }

  void print_logs()
  {
    string prefix = NAME + " ";
    if(cache_is[CACHE_ID::IS_LLC])
    {
      int rd_avg = 0;
      int wr_avg = 0;
      int total_prefetch_cached_block = 0;
      for(int i=0; i< NUM_WAY*NUM_SET; i++)
      {
        rd_avg += prefetch_hit_histo[i][READ_HIT];
        wr_avg += prefetch_hit_histo[i][WRITEBACK_HIT];
      }
      rd_avg = (double)rd_avg/(double)(NUM_WAY*NUM_SET);
      cout << prefix << "prefetch block read access average, " << (double)rd_avg/(double)(NUM_WAY*NUM_SET) << '\n';

      wr_avg = (double)wr_avg/(double)(NUM_WAY*NUM_SET);
      cout << prefix << "prefetch block write access average, " << wr_avg  << '\n';

      int rd_var = 0, wr_var = 0;
      for(int i=0; i< NUM_WAY*NUM_SET; i++)
      {
        int delta = rd_avg - prefetch_hit_histo[i][READ_HIT];
        rd_var += delta * delta;

        delta = wr_avg - prefetch_hit_histo[i][WRITEBACK_HIT];
        wr_var += delta * delta;
      }
      rd_var = (double)rd_var/(double)(NUM_WAY*NUM_SET - 1);
      wr_var = (double)wr_var/(double)(NUM_WAY*NUM_SET - 1);

      cout << "prefetch block read access variance, " << rd_var << '\n';
      cout << "prefetch block wr access variance, " << wr_var << '\n';
    }
  }

#include "cache_modules.inc"

  const repl_t repl_type;
  const pref_t pref_type;

  // constructor
  CACHE(std::string v1, double freq_scale, unsigned fill_level, uint32_t v2, int v3, uint32_t v5, uint32_t v6, uint32_t v7, uint32_t v8, uint32_t hit_lat,
        uint32_t fill_lat, uint32_t max_read, uint32_t max_write, std::size_t offset_bits, bool pref_load, bool wq_full_addr, bool va_pref,
        unsigned pref_act_mask, MemoryRequestConsumer* ll, pref_t pref, repl_t repl)
      : champsim::operable(freq_scale), MemoryRequestConsumer(fill_level), MemoryRequestProducer(ll), NAME(v1), NUM_SET(v2), NUM_WAY(v3), WQ_SIZE(v5),
        RQ_SIZE(v6), PQ_SIZE(v7), MSHR_SIZE(v8), HIT_LATENCY(hit_lat), FILL_LATENCY(fill_lat), OFFSET_BITS(offset_bits), MAX_READ(max_read),
        MAX_WRITE(max_write), prefetch_as_load(pref_load), match_offset_bits(wq_full_addr), virtual_prefetch(va_pref), pref_activate_mask(pref_act_mask),
        repl_type(repl), pref_type(pref)
  {

    prefetch_hit_histo = (int**)malloc(sizeof(int*) * NUM_WAY * NUM_SET);
    for(int i=0; i< NUM_WAY*NUM_SET; i++)
    {
      prefetch_hit_histo[i] = (int*)malloc(sizeof(int) * 2);
      for(int j=0; j< 2; j++)
      {
        prefetch_hit_histo[i][j] = 0;
      }
    }  

    cacheDataModel = new CacheDataModel(NAME, cpu);
    
    WRITE_LANTENCY = hit_lat;

    if(NAME.find("LLC") != string::npos)
    {
      cache_is[CACHE_ID::IS_LLC] = true;
    }
    else if(NAME.find("STLB") != string::npos)
    {
      cache_is[CACHE_ID::IS_STLB] = true;
    }
    else if(NAME.find("DTLB") != string::npos)
    {
      cache_is[CACHE_ID::IS_DTLB] = true;
    }
    else if(NAME.find("ITLB") != string::npos)
    {
      cache_is[CACHE_ID::IS_ITLB] = true;
    }
  }

  ~CACHE()
  {
    cacheDataModel->print_stats();
  }
};

#endif
