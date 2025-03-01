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



// STT_MRAM
int KNOB_STTMRAM_STLB = 0;

// Page table
int KNOB_ENABLE_PT_OPTIMIZATION = 1; // default is allow use of opt ptw; works faster 

extern CACHE* Buffer = new CACHE (
    "Buffer", 1.0, 6, 
    1, 1500, 16, 
    16, 0, 0, 
    8, 1, 1,
    1, LOG2_BLOCK_SIZE, 0,
    0, 0, 5, 
    &DRAM, CACHE::pref_t::pprefetcherDno, CACHE::repl_t::rreplacementDlru);

#endif