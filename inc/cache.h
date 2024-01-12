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

#include "V1Predictor.h"
#include "CacheStat.h"
#include "constant.h"
#include <math.h>

#include "../plugin/info.h"
#include "../plugin/OfflineEdgeLifetime.h"
#include "../plugin/OfflineEdgeBreak.h"

// virtual address space prefetching
#define VA_PREFETCH_TRANSLATION_LATENCY 2

extern std::array<O3_CPU*, NUM_CPUS> ooo_cpu;

class CACHE : public champsim::operable, public MemoryRequestConsumer, public MemoryRequestProducer
{
public:
  uint32_t cpu;
  const std::string NAME;
  const uint32_t NUM_SET, NUM_WAY, WQ_SIZE, RQ_SIZE, PQ_SIZE, MSHR_SIZE;
  const uint32_t HIT_LATENCY, FILL_LATENCY, OFFSET_BITS, RD_LATENCY, WR_LATENCY;
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
      WQ{WQ_SIZE, HIT_LATENCY};                           // write queue

  std::list<PACKET> MSHR; // MSHR

  uint64_t sim_access[NUM_CPUS][NUM_TYPES] = {}, sim_hit[NUM_CPUS][NUM_TYPES] = {}, sim_miss[NUM_CPUS][NUM_TYPES] = {}, roi_access[NUM_CPUS][NUM_TYPES] = {},
           roi_hit[NUM_CPUS][NUM_TYPES] = {}, roi_miss[NUM_CPUS][NUM_TYPES] = {};

  uint64_t RQ_ACCESS = 0, RQ_MERGED = 0, RQ_FULL = 0, RQ_TO_CACHE = 0, PQ_ACCESS = 0, PQ_MERGED = 0, PQ_FULL = 0, PQ_TO_CACHE = 0, WQ_ACCESS = 0, WQ_MERGED = 0,
           WQ_FULL = 0, WQ_FORWARD = 0, WQ_TO_CACHE = 0;

  uint64_t total_miss_latency = 0;


  // ***Pravesh
  uint32_t read_count=0,  write_count=0;
  uint32_t sim_total_access=0, roi_total_access=0;
  uint32_t sim_total_miss=0, roi_total_miss=0;

  bool is_llc=false;

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
  uint32_t get_tag(uint64_t address);
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


  public:
  /**
   * @author Pravesh
   * @brief after successful writes do bookkeping here
   */
  void post_write_success(PACKET& pkt, WRITE_TYPE write, int set, int way, bool cacheHit);

  PREDICTION pre_write_action(PACKET& pkt, int set, RESULT result);

  /*if block is present (hit), and it is DEAD (upon readmiss) then set it to ALIVE. And if it is ALIVE, then keep as it is*/
  void update_blocks_life(BLOCK* block, bool is_block_hit){
    if(is_block_hit){
      block->packet_life = PACKET_LIFE::ALIVE;
    }
  }

  IntPtr get_cycle_number(int cpu){
    ooo_cpu[cpu]->current_cycle;
  }

  double get_assymetric_read_write_latency(){
    return read_count*RD_LATENCY + write_count*WR_LATENCY;
  }

  void post_read_success(int set, int way, bool cacheHit);
  void apply_bypass_on_writeback(int set, int way, PACKET& writeback);
  void apply_bypass_on_fillback(int set, int way, PACKET& fill_mshr);

  IPredictor* ipredictor;
  CacheStat* cacheStat;
  Info* info;
  EpocManager* cacheEpocManager;
  OfflineEdgeLifetime* offline;
  OfflineEdgeBreak* offline_edge_break;

  int random_set[10000] = {0};
  int rnd_sets = 0;

  void initalize_extras(IPredictor* predictor){
    ipredictor = predictor;
    cacheStat = new CacheStat();
  }

  /*should be called only before print() call of  CacheStat*/
  void write_profile(){
    IntPtr total_block = NUM_WAY*NUM_SET;

    string s = "llc_matrix.log";
    fstream mtx = Log::get_file_stream(s);

    for(int i=0; i< NUM_SET; i++)
    {
      for(int j=0; j< NUM_WAY; j++)
      {
        mtx << block[i*NUM_WAY + j].writes << ',';
      }
      mtx << '\n';
    }

    double total_writes_by_cacheStat = cacheStat->get_total_writes();
    double avg_by_cacheStat = (double)total_writes_by_cacheStat/total_block;
    
    IntPtr total_write = 0;
    double total_set_expect = 0;

    for(int set=0; set< NUM_SET; set++){
      
      // per set ==> total write, and avg write per block
      IntPtr per_set_write = 0;
      double per_set_per_block_avg_write = 0;
      for(int way=0; way< NUM_WAY; way++){
        per_set_write += block[set * NUM_WAY + way].writes;
      }
      per_set_per_block_avg_write = (double)per_set_write/(double)NUM_WAY;

      // sum of squared expectation value
      double per_set_expect = 0;
      for(int way=0; way< NUM_WAY; way++){
        // expectation
        double expect = block[set * NUM_WAY + way].writes - per_set_per_block_avg_write;
        // square
        expect = expect * expect;
        // total
        per_set_expect += expect;
      }

      per_set_expect = per_set_expect/(NUM_WAY-1);
      per_set_expect = sqrt(per_set_expect);
      total_set_expect += per_set_expect;
      total_write += per_set_write;
    }

    double avg_write_per_block = (double)total_write/(double)total_block;
    double avg_write_per_set = (double)total_write/(double)NUM_SET;
    double intra = ((double)1/(double)(NUM_SET*avg_write_per_block)) * total_set_expect;

    total_set_expect = 0;
    for(int set=0; set< NUM_SET; set++){
      IntPtr per_set_write = 0;
      double per_set_per_block_avg_write = 0;
      for(int way=0; way< NUM_WAY; way++){
        per_set_write += block[set * NUM_WAY + way].writes;
      }
      per_set_per_block_avg_write = (double)per_set_write/(double)NUM_WAY;

      double per_set_expect = 0;
      per_set_expect = per_set_per_block_avg_write - avg_write_per_block;
      per_set_expect = per_set_expect * per_set_expect;
      total_set_expect = total_set_expect + per_set_expect;
    }
    total_set_expect = total_set_expect/(double)(NUM_SET-1);
    total_set_expect = sqrt(total_set_expect);
    double inter = total_set_expect/avg_write_per_block;
    cacheStat->store_nvm_profile(intra, inter, avg_write_per_block, avg_write_per_set, WQ.get_latency(), RQ.get_latency());

    cout<< "*****************************************************\n";
    cout << "By CacheStat, By block\n";
    cout<<"total:"<<total_writes_by_cacheStat<<","<<total_write<<'\n';
    cout<<"avg:"<<avg_by_cacheStat<<","<<avg_write_per_block<<'\n';
    cout<< "*****************************************************\n";
  }

  void compute_total_access(){
    sim_total_access = sim_total_miss = 0;
    roi_total_access = roi_total_miss = 0;

    for(int i=0; i< NUM_TYPES; i++){
      sim_total_access += sim_access[cpu][i];
      roi_total_access += roi_access[cpu][i];

      sim_total_miss += sim_miss[cpu][i];
      roi_total_miss += roi_miss[cpu][i];
    }
  }

#include "cache_modules.inc"

  const repl_t repl_type;
  const pref_t pref_type;

  // constructor
  CACHE(std::string v1, double freq_scale, unsigned fill_level, 
      uint32_t v2, int v3, uint32_t v5, uint32_t v6, uint32_t v7, uint32_t v8, 
      uint32_t hit_lat, uint32_t rd_latency, uint32_t wr_latency, uint32_t fill_lat, 
      uint32_t max_read, uint32_t max_write, 
      std::size_t offset_bits, bool pref_load, bool wq_full_addr, bool va_pref,
      unsigned pref_act_mask, MemoryRequestConsumer* ll, pref_t pref, repl_t repl)
      : champsim::operable(freq_scale), MemoryRequestConsumer(fill_level), MemoryRequestProducer(ll), NAME(v1), NUM_SET(v2), NUM_WAY(v3), WQ_SIZE(v5),
        RQ_SIZE(v6), PQ_SIZE(v7), MSHR_SIZE(v8), 
        HIT_LATENCY(hit_lat), RD_LATENCY(rd_latency), WR_LATENCY(wr_latency),
        FILL_LATENCY(fill_lat), OFFSET_BITS(offset_bits), MAX_READ(max_read),
        MAX_WRITE(max_write), prefetch_as_load(pref_load), match_offset_bits(wq_full_addr), virtual_prefetch(va_pref), pref_activate_mask(pref_act_mask),
        repl_type(repl), pref_type(pref)
  {
    cacheStat = nullptr;
    ipredictor = nullptr;
    if(NAME.find("LLC")!=string::npos)
    {
      is_llc = true;
      std::cout << fill_lat << "," << rd_latency << "," << wr_latency << '\n';
      info = new Info();
      cacheEpocManager = new EpocManager(current_cycle);
      rnd_sets = NUM_SET/4;

      for(int i=0; i< rnd_sets; i++)
      {
        int set_no = rand()%rnd_sets;
        random_set[set_no] = 1;
      }

      offline = new OfflineEdgeLifetime();
      offline_edge_break = new OfflineEdgeBreak();
    }
  }
};

#endif
