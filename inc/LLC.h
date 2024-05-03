#ifndef LLC_H
#define LLC_H

#include "memory_class.h"
#include "cache.h"
#include <iostream>
#include <vector>

class LLC:public MEMORY
{
  public:
  vector<CACHE> banks;
  int  add_rq(PACKET *packet);  //The = 0 after a virtual function means that "this is pure virtual function, it must be implemented in the derived function".
  int  add_wq(PACKET *packet);
  int  add_pq(PACKET *packet);
  void return_data(PACKET *packet);
  void operate();
  void increment_WQ_FULL(uint64_t address);
  uint32_t get_occupancy(uint8_t queue_type, uint64_t address);
  uint32_t get_size(uint8_t queue_type, uint64_t address);

  LLC();
};

#endif