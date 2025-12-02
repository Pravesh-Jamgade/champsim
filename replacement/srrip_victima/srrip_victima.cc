#include "cache.h"

#define maxRRPV 3
extern int KNOB_ENABLE_SWAT_WAYS;

// initialize replacement state
void CACHE::initialize_replacement()
{
  std::cout << "SWAT Replacement SWAT_WAYS=" << KNOB_ENABLE_SWAT_WAYS << '\n';
  for (auto& blk : block)
    blk.lru = maxRRPV;
}

// find replacement victim
uint32_t CACHE::find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK* current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
  // look for the maxRRPV line && make sure we dont give out sector line
  auto begin = std::next(std::begin(block), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);

  // Try to get NormalBlock if possible
  int k_times = 5;
  while(k_times--)
  {
    // if victima block then try k times to get normal block
    for (auto it = begin; it != end; ++it)
    {
      // VictimaBlock
      if(it->victima_block) continue;
      // NormalBlock
      if(it->lru >= maxRRPV) continue;
      it->lru++;
    }

    auto victim = std::find_if(begin, end, [](BLOCK x) {return x.lru == maxRRPV;});

    // if NormalBlock then simply return
    for(; victim != end; victim++)
    {
      if(!victim->victima_block)
        return std::distance(begin, victim);
    }
  }

  // search until we get maxRRPV value block
  auto victim = std::find_if(begin, end, [](BLOCK x) { return x.lru == maxRRPV;});
  while (victim == end) 
  {
    // increase lru
    for (auto it = begin; it != end; ++it)
    {
      if(it->lru >= maxRRPV) break;
      it->lru++;
    }

    victim = std::find_if(begin, end, [](BLOCK x) { return x.lru == maxRRPV;});
  }

  return std::distance(begin, victim);
}

// called on every cache hit and cache fill
void CACHE::update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type,
                                     uint8_t hit)
{
  if (hit)
    block[set * NUM_WAY + way].lru = 0;
  else
    block[set * NUM_WAY + way].lru = maxRRPV - 1;
}

// use this function to print out your own stats at the end of simulation
void CACHE::replacement_final_stats() {}
