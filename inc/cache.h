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
#include "epoch.h"

#define INVL_BUFF_SIZE 32

// virtual address space prefetching
#define VA_PREFETCH_TRANSLATION_LATENCY 2

extern int KNOB_VWAY;

extern std::array<O3_CPU*, NUM_CPUS> ooo_cpu;

class CACHE : public champsim::operable, public MemoryRequestConsumer, public MemoryRequestProducer
{
public:
  //usercode
  CacheDataModel* cacheDataModel;
  list<BLOCK>* reuse_history;

  // TODO: dummy, not storing data, except counts of writes
  Epoc* epoc;
  uint64_t vway_counter[VWAY_COUNTER::VWAY_COUNTER_END] = {0};
  std::vector<BLOCK> data_arr;
  vector<BLOCK>::iterator vway_head;
  vector<BLOCK>::iterator vway_tail;
  int* set_avg_write;
  int* set_total_write;
  enum VWAY_HOLE_OPT{LEAVE_HOLE=0, INC_TO_HOLE, TAIL_TO_HOLE};
  std::list<BLOCK> invalid_buffer;

  bool cache_is[CACHE_ID_END] = {false};
  list<BLOCK> fa_array;

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
  int FA_SIZE =0;

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

  void readlike_hit(std::size_t set, std::size_t way, PACKET& handle_pkt, BLOCK* data=nullptr);
  bool readlike_miss(PACKET& handle_pkt);
  bool filllike_miss(std::size_t set, std::size_t way, PACKET& handle_pkt);

  bool should_activate_prefetcher(int type);

  void print_deadlock() override;

  void* getObject(){return this;}

  // v-way
  BLOCK* vway_handle_tag_replacement();
  BLOCK* vway_get_head();

  void func_track_reuse(uint32_t set, uint32_t way);

  void reset_datamodel()
  {
    delete cacheDataModel;
    cacheDataModel = new CacheDataModel(NAME, cpu, NUM_SET, NUM_WAY);
  }

  BLOCK* tag_search(size_t set, PACKET packet);

  bool func_set_full(size_t set)
  {
    auto set_begin = std::next(std::begin(block), set * NUM_WAY);
    auto set_end = std::next(set_begin, NUM_WAY);
    auto first_inv = std::find_if_not(set_begin, set_end, is_valid<BLOCK>());
    uint32_t way = std::distance(set_begin, first_inv);
    return way == NUM_WAY;
  }

  void func_insert_invalid_buffer(BLOCK* buff)
  {
    if(invalid_buffer.size() == INVL_BUFF_SIZE)
    {
      invalid_buffer.front().bptr->valid = 0;
      invalid_buffer.front().bptr->fptr = nullptr;
      invalid_buffer.pop_front();
    }
    invalid_buffer.push_back(*buff);
    buff->fptr = &invalid_buffer.back();
    invalid_buffer.back().bptr = buff;
  }

  bool func_search_invalid_buffer(PACKET packet)
  {
    auto find_data = find_if(invalid_buffer.begin(), invalid_buffer.end(), eq_addr<BLOCK>(packet.address, OFFSET_BITS));
    return (find_data != invalid_buffer.end());
  }

  void func_write_variation(std::vector<BLOCK> temp_arr)
  {
    int intra_set_wv = 0;
    int inter_set_wv = 0;
    int total_avg_write = 0;
    for(int i=0; i< NUM_SET; i++)
    {
      int per_set = 0;
      for(int j=0; j< NUM_WAY; j++)
      {
        per_set += temp_arr[i*NUM_WAY + j].data_write;
      }

      set_total_write[i] = per_set;
      set_avg_write[i] = per_set/NUM_WAY;

      for(int k=0; k< NUM_WAY; k++)
      {
        int diff = abs(set_avg_write[i] - temp_arr[i*NUM_WAY + k].data_write);
        intra_set_wv += diff * diff;
      }

      total_avg_write += per_set;
    }
    total_avg_write /= (NUM_SET * NUM_WAY);

    for(int i=0; i< NUM_SET; i++)
    {
      int diff = abs(set_avg_write[i] - total_avg_write);
      inter_set_wv += NUM_WAY * diff * diff;
    }

    cout << "inter_set write variation, " << inter_set_wv << '\n';
    cout << "intra_set write variation, " << intra_set_wv << '\n';
    cout << "total avg write per block, " << total_avg_write << '\n';
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

      if(cache_is[CACHE_ID::IS_LLC])
      {
        int total_data_array_write = 0;
        if(KNOB_VWAY)
        {
          for(int i=0; i< NUM_WAY * NUM_SET; i++)
          {
            total_data_array_write += data_arr[i].data_write;
          }
        }
        else
        {
          for(int i=0; i< NUM_WAY * NUM_SET; i++)
          {
            total_data_array_write += block[i].data_write;
          }
        }
        cout << "data_arr write, " << total_data_array_write << '\n';

        cout << "data_arr write_variation\n";
        if(KNOB_VWAY)
          func_write_variation(data_arr);
        else
          func_write_variation(block);
      }
    }
    cout << '\n';
    
    for(int i=0; i< VWAY_COUNTER::VWAY_COUNTER_END; i++)
    {
      cout << NAME << " " << vway_counter_str[i] << ", " << vway_counter[i] << '\n'; 
    }

    cout << '\n';
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
    
    // epoc = new Epoc();

    set_total_write = (int*)malloc(sizeof(int)*NUM_SET);
    set_avg_write = (int*)malloc(sizeof(int)*NUM_SET);

    for(int i=0; i< NUM_SET; i++)
    {
      set_total_write[i] = 0;
      set_avg_write[i] = 0;
    }

    for(int i=0; i< NUM_WAY*NUM_SET; i++)
    {
      data_arr.push_back(BLOCK());
      data_arr.back().bid = i;
    }

    vway_head = data_arr.end();
    vway_tail = data_arr.end();

    reuse_history = new list<BLOCK>[NUM_SET];
    for(int i=0; i< NUM_SET; i++)
      reuse_history[i] = list<BLOCK>();

    prefetch_hit_histo = (int**)malloc(sizeof(int*) * NUM_WAY * NUM_SET);
    for(int i=0; i< NUM_WAY*NUM_SET; i++)
    {
      prefetch_hit_histo[i] = (int*)malloc(sizeof(int) * 2);
      for(int j=0; j< 2; j++)
      {
        prefetch_hit_histo[i][j] = 0;
      }
    }  

    cacheDataModel = new CacheDataModel(NAME, cpu, NUM_SET, NUM_WAY);
    
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

    FA_SIZE = NUM_WAY * NUM_SET;
  }

  ~CACHE()
  {
    if(cache_is[CACHE_ID::IS_STLB])
      cacheDataModel->print_end_stats();
  }
};

#endif
