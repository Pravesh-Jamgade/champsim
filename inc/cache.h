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
#include <map>
#include "DataModel.h"
#include "victima.h"
#include "user.h"
#include"logger.h"
#include <bitset>
#include "pollution.h"

extern int KNOB_ENABLE_LOG;

extern map<tuple<uint64_t, int>, PTWC> ptw_pred;

// virtual address space prefetching
#define VA_PREFETCH_TRANSLATION_LATENCY 2

extern std::array<O3_CPU*, NUM_CPUS> ooo_cpu;

class CACHE : public champsim::operable, public MemoryRequestConsumer, public MemoryRequestProducer
{
public:

  map<tuple<uint64_t, int>, uint64_t> record_stlbmiss;

  // 16 threads
  // 8 possible offsets
  // corresponding PTE
  ThreadBucket collect_pte[16];
  // std::mt19937 rng(42);

  vector<vector<PollutionEntry>> global_set_history;

  // translation pollution
  TranslationPollution* translation_pollution;
  VictimaPollution* victima_pollution;

  // record evcited PTE
  const int LIMIT_HITORY_LEN_EVICTED_PTE = 64;
  map<uint64_t, uint64_t> eviction_history_pte;

  // offset VS number of times this offset seen 
  vector<vector<int>> transition_hitmap_for_offset;

  // distance VS number of times this offset seen 
  vector<vector<int>> transition_hitmap_for_vp_page, transition_hitmap_for_pp_page;

  logger debugLog;
  logger dlog;
  logger dassert;

  //usercode
  bool is_tlb = false;
  list<BLOCK>* reuse_history;
  // storing tag and last global access count
  unordered_map<uint64_t, uint64_t> global_reuse;
  uint64_t global_access_count = 0;

  enum VC
  {
    STLB_EVICT=0,
    VICTIMA_PTW_COUNT,
    STLB_VICTIMA_HIT,
    STLB_PTW_HIT,
    L2_EVICT,
    L2_WRITE,
    L2_READ_HIT,
    L2_READ_MISS,
    STLB_MSHRRECV_DROP_VICTIMA,
    STLB_MSHRMISS_DROP_VICTIMA,
    STLB_ZERO_DROP_VICTIMA,
    STLB_ZERO_DROP_PTW,
    STLB_MSHRMISS_DROP_PTW,
    STLB_MSHRRECV_DROP_PTW,
    STLB_DUMY_VICTIMA,
    STLB_DUMY_PTW,
    VC_END
  };
  //usercode
  int victima_counters[VC_END] = {0};
  int victima_block_usage[9] = {0};

  MemoryRequestConsumer *l2cache, *l1cache;
  CacheDataModel* cacheDataModel;
  // track working set for counting capacity misses
  unordered_map<uint64_t, bitset<64>> page_to_block;

  bool cache_is[CACHE_ID_END] = {false};
  CACHE_ID cache_id = CACHE_ID::CACHE_ID_END;
  list<BLOCK> fa_array;
  int FA_SIZE =0;

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

  uint32_t get_set(int type, uint64_t address, bool victima=false);
  uint32_t get_way(int type, uint64_t address, uint32_t set, int thread_id, bool victima=false);

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

  uint64_t use_offset(int type);

  pair<bool, PTEHolder> victima_peek_singleline(const PACKET handle_pkt);

  // tracking pte
  void func_track_evicted_pte(uint64_t v_addr, uint64_t p_addr);

  // track accessed page and its blocks for tracking capacity misses
  void func_track_workingset(uint64_t addr);

  // track mshr waiting period for packet
  void func_track_missfulfill_access_latency(uint64_t enq_cycle);
  // track miss access latency 
  void func_track_miss_access_latency(uint64_t enq_cycle);
  // track hit access latency 
  void func_track_hit_access_latency(uint64_t enq_cycle, int metadata=0);

  void func_return(PACKET* packet);

  CacheBlock* func_test_page_table(BLOCK& fill_block);

  int get_pte_offset(uint64_t addr)
  {
    return ((addr >> 3) & 0x7);
  }

  void _context_switch(int thread_id) 
  {
    
    if(is_tlb)
    {
      for(auto& cb: block)
      {
        if(cb.thread_id == thread_id)
        {
          cb.address = 0;
          cb.data = 0;
          cb.valid = 0;
        }
      }
    }
  }

  void reset_datamodel()
  {
    delete cacheDataModel;
    cacheDataModel = new CacheDataModel(NAME, cpu, NUM_WAY);
  }

  bool victima_lookup(uint64_t addr, int cpu)
  {
    uint64_t page = addr & ~(PAGE_SIZE-1);
    auto found = ptw_pred.find({page,cpu});
    if(found != ptw_pred.end())
    {
      int cost = found->second.cost;
      int freq = found->second.freq;
      return (freq >= 1 && freq <= 2 && cost >=1 && cost<=2);
    }

    dassert.log("Error !, Victima lookup not found page", "addr", intToHex(addr), "page", intToHex(page), "th", cpu, '\n');
    // page not there
    exit(-1);
    return false;
  }

  int add_to_cluster(PACKET* packet);
  void adjust_hashcache();
  int use_cluster(PACKET* packet);

  void print_logs()
  {
    string prefix = NAME + " ";
    if(cache_is[CACHE_ID::IS_LLC])
    {
      int rd_avg = 0;
      int wr_avg = 0;
      for(int i=0; i< NUM_WAY*NUM_SET; i++)
      {
        rd_avg += prefetch_hit_histo[i][READ_HIT];
        wr_avg += prefetch_hit_histo[i][WRITEBACK_HIT];
      }
      cout << prefix << "total prefetch block read access, " << rd_avg << '\n';
      rd_avg = (double)rd_avg/(double)(NUM_WAY*NUM_SET);
      cout << prefix << "prefetch block read access average, " << (double)rd_avg/(double)(NUM_WAY*NUM_SET) << '\n';

      cout << prefix << "total prefetch block write access, " << wr_avg << '\n';
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
    
    if(cache_is[IS_L2])
    {
      cout << NAME << "\n<<<<<<<<<<<<<<< Victima Counters L2 >>>>>>>>>>>>>>>\n";
      cout << "victima l2 evict, " << victima_counters[L2_EVICT] << '\n'; 
      cout << "victima l2 write, " << victima_counters[L2_WRITE] << '\n'; 
      cout << "victima l2 read hit, " << victima_counters[L2_READ_HIT] << '\n'; 
      cout << "victima l2 read miss, " << victima_counters[L2_READ_MISS] << '\n'; 

      cout << "\nvictima cache block usage @ L2 cache\n";
      for(int i=1; i< 9; i++)
      {
        cout << "victima_block_usage " << i << ", " << victima_block_usage[i] << '\n';
      }

      cout << NAME << "\n<<<<<<<<<<<<<<< 0 >>>>>>>>>>>>>>>\n";
      // print pollution
      translation_pollution->print(NAME);
      victima_pollution->print(NAME);
      cout << NAME << "\n<<<<<<<<<<<<<<< 0 >>>>>>>>>>>>>>>\n";
    }
    if(cache_is[IS_STLB])
    {
      cout << '\n';
      cout << "victima stlb eivct, " << victima_counters[STLB_EVICT] << '\n'; 
      cout << "victima PTW's, " << victima_counters[VICTIMA_PTW_COUNT] << '\n'; 
      cout << '\n';

      cout << "Transition hitmap for offset counter over windows:\n";
      cout << "Offset V/s frequency_of_offset\n\n";

      // Print column headers
      cout << setw(6) << " " << "|";
      for (int i = 0; i < transition_hitmap_for_offset[0].size(); ++i) {
          cout << setw(4) << i;
      }
      cout << '\n';

      // Print separator line
      cout << string(6, '-') << "+";
      for (int i = 0; i < transition_hitmap_for_offset[0].size(); ++i) {
          cout << string(4, '-');
      }
      cout << '\n';

      // Print each row
      for (int i = 0; i < 8; ++i) {
          cout << setw(6) << i << "|";
          for (int j = 0; j < transition_hitmap_for_offset[i].size(); ++j) {
              cout << setw(4) << transition_hitmap_for_offset[i][j] << ',';
          }
          cout << '\n';
      }


      cout << "(Virtual Page) Transition hitmap for distance between evicted page over window:\n";
      cout << "Virtual Page: Distance V/s frequency_of_distance\n\n";

      // Print column headers
      cout << setw(6) << " " << "|";
      for (int i = 0; i < transition_hitmap_for_vp_page[0].size(); ++i) {
          cout << setw(4) << i;
      }
      cout << '\n';

      // Print separator line
      cout << string(6, '-') << "+";
      for (int i = 0; i < transition_hitmap_for_vp_page[0].size(); ++i) {
          cout << string(4, '-');
      }
      cout << '\n';

      // Print each row
      for (int i = 0; i < 8; ++i) {
          cout << setw(6) << i << "|";
          for (int j = 0; j < transition_hitmap_for_vp_page[i].size(); ++j) {
              cout << setw(3) << transition_hitmap_for_vp_page[i][j] << ',';
          }
          cout << '\n';
      }

      cout << "(Physical Page) Transition hitmap for distance between evicted page over window:\n";
      cout << "Physical Page: Distance V/s frequency_of_distance\n\n";

      // Print column headers
      cout << setw(6) << " " << "|";
      for (int i = 0; i < transition_hitmap_for_pp_page[0].size(); ++i) {
          cout << setw(3) << i;
      }
      cout << '\n';

      // Print separator line
      cout << string(6, '-') << "+";
      for (int i = 0; i < transition_hitmap_for_pp_page[0].size(); ++i) {
          cout << string(4, '-');
      }
      cout << '\n';

      // Print each row
      for (int i = 0; i < 8; ++i) {
          cout << setw(6) << i << "|";
          for (int j = 0; j < transition_hitmap_for_pp_page[i].size(); ++j) {
              cout << setw(3) << transition_hitmap_for_pp_page[i][j] << ',';
          }
          cout << '\n';
      }
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

    for(int i=0; i< 16; i++)
    {
      collect_pte[i] = ThreadBucket();
    }
    
    debugLog = logger(false);
    dlog = logger(false);
    dassert = logger(true);

    global_set_history = vector<vector<PollutionEntry>>(NUM_SET, vector<PollutionEntry>(4*NUM_WAY));

    translation_pollution = new TranslationPollution(NUM_SET, NUM_WAY, &global_set_history);
    victima_pollution = new VictimaPollution(NUM_SET, NUM_WAY, &global_set_history);

    transition_hitmap_for_vp_page = vector<vector<int>>(8);
    for(int i=0; i< 8; i++)
      transition_hitmap_for_vp_page[i] = vector<int>(9, 0);

    transition_hitmap_for_pp_page = vector<vector<int>>(8);
    for(int i=0; i< 8; i++)
      transition_hitmap_for_pp_page[i] = vector<int>(9, 0);

    transition_hitmap_for_offset = vector<vector<int>>(8);
    for(int i=0; i< 8; i++)
      transition_hitmap_for_offset[i] = vector<int>(LIMIT_HITORY_LEN_EVICTED_PTE+1, 0);

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

    cacheDataModel = new CacheDataModel(NAME, cpu, NUM_WAY);
    
    WRITE_LANTENCY = hit_lat;

    if(NAME.find("LLC") != string::npos)
    {
      cache_is[CACHE_ID::IS_LLC] = true;
      cache_id = CACHE_ID::IS_LLC;
    }
    else if(NAME.find("L2") != string::npos)
    {
      cache_is[CACHE_ID::IS_L2] = true;
      cache_id = CACHE_ID::IS_L2;
    }
    else if(NAME.find("L1D") != string::npos)
    {
      cache_is[CACHE_ID::IS_L1D] = true;
      cache_id = CACHE_ID::IS_L1D;
    }
    else if(NAME.find("L1I") != string::npos)
    {
      cache_is[CACHE_ID::IS_L1I] = true;
      cache_id = CACHE_ID::IS_L1I;
    }
    else if(NAME.find("STLB") != string::npos)
    {
      cache_is[CACHE_ID::IS_STLB] = true;
      cache_id = CACHE_ID::IS_STLB;
      is_tlb = true;
    }
    else if(NAME.find("DTLB") != string::npos)
    {
      cache_is[CACHE_ID::IS_DTLB] = true;
      cache_id = CACHE_ID::IS_DTLB;
      is_tlb = true;
    }
    else if(NAME.find("ITLB") != string::npos)
    {
      cache_is[CACHE_ID::IS_ITLB] = true;
      cache_id = CACHE_ID::IS_ITLB;
      is_tlb = true;
    }

    FA_SIZE = NUM_WAY * NUM_SET;
  }

  ~CACHE()
  {
    cacheDataModel->print_stats();
  }
};

#endif
