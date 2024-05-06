#ifndef LLC_H
#define LLC_H

#include "memory_class.h"
#include <iostream>
#include <vector>
#define NUM_BANKS 4

/*
RQ,WQ -> we wont be using them, they will only redirect to bank. As we want to have bank contention effect on L2
MSHR -> we will use as DRAM will return data here, the missed experienced by sub-bank is tracked by these MSHR

*/
class LLC:public MEMORY
{
  public:
  string NAME = "LLC_master";
  uint32_t QUEUE_SIZE = 1;
  uint32_t MSHR_SIZE = 32;// should be what is needed by LLC
  uint32_t LATENCY;
   // queues
    PACKET_QUEUE WQ{NAME + "_WQ", QUEUE_SIZE}, // write queue
                 RQ{NAME + "_RQ", QUEUE_SIZE}, // read queue
                 PQ{NAME + "_PQ", QUEUE_SIZE}, // prefetch queue   /*object of class PACKET_QUEUE which is in the block.h file*/
                 MSHR{NAME + "_MSHR", MSHR_SIZE}, // MSHR
                 PROCESSED{NAME + "_PROCESSED", ROB_SIZE}; // processed queue

  int  add_rq(PACKET *packet);  //The = 0 after a virtual function means that "this is pure virtual function, it must be implemented in the derived function".
  int  add_wq(PACKET *packet);
  int  add_pq(PACKET *packet);
  void return_data(PACKET *packet);
  void operate();
  void increment_WQ_FULL(uint64_t address);
  uint32_t get_occupancy(uint8_t queue_type, uint64_t address);
  uint32_t get_size(uint8_t queue_type, uint64_t address);

  uint32_t get_bank_no(uint32_t set);
  uint32_t get_set(uint64_t address);

  int check_mshr(PACKET *packet);
  void add_mshr(PACKET *packet);
  void update_fill_cycle();

  void log()
  {
    cout << "inside\n";
  }

  LLC();
  ~LLC(){}

};

extern LLC *llc;
extern bool tracing_on;
extern bool one_packet;

#endif