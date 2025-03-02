#ifndef USER_H
#include <iostream>
#include "cache.h"
#include "dramsim3_wrapper.hpp"
#include "dram_controller.h"
extern DRAMSim3_DRAM DRAM;
using namespace std;

int KNOB_TRANSLATION_QUEUE = 0;
int KNOB_TTP = 0;
int KNOB_STLB_DO_NOT_TRACK_MISS = 0;

// Buffer
int KNOB_ENABLE_LLC_BUFFER = 0;

// STT_MRAM
int KNOB_STTMRAM_STLB = 0;

// Page table
int KNOB_ENABLE_PT_OPTIMIZATION = 1; // default is allow use of opt ptw; works faster 

extern CACHE* Buffer = new CACHE (
    "Buffer", 1.0, 6, 
    1, 1500, 16, 
    16, 0, 0, 
    1, 1, 4,
    1, LOG2_BLOCK_SIZE, 0,
    0, 0, 5, 
    &DRAM, CACHE::pref_t::pprefetcherDno, CACHE::repl_t::rreplacementDlru);

// CACHE(
  //   std::string v1, double freq_scale, unsigned fill_level, 
  //   uint32_t NUM_SET (v2), int NUM_WAY (v3), uint32_t WQ_SIZE (v4), 
  //   uint32_t RQ_SIZE (v5), uint32_t PQ_SIZE(v7), uint32_t MSHR_SIZE(v8),
  //   uint32_t hit_lat, uint32_t fill_lat, uint32_t max_read, 
  //   uint32_t max_write, std::size_t offset_bits, bool pref_load, 
  //   bool wq_full_addr, bool va_pref, unsigned pref_act_mask, 
  //   MemoryRequestConsumer* ll, pref_t pref, repl_t repl)
  


#endif