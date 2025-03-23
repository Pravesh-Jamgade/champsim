#ifndef USER_H
#include <iostream>
using namespace std;

int KNOB_TRANSLATION_QUEUE = 0;
int KNOB_TTP = 0;
int KNOB_STLB_DO_NOT_TRACK_MISS = 0;



// STT_MRAM
int KNOB_STTMRAM_STLB = 0;

// Page table
int KNOB_ENABLE_PT_OPTIMIZATION = 1; // default is allow use of opt ptw; works faster 

// MSHR Sublocking
int KNOB_MSHR_SUBLOCK = 0;
int KNOB_LOG_CLUSTER_SIZE = 0;

#endif