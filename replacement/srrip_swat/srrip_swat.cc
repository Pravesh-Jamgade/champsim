#include "cache.h"

#define maxRRPV 3
static int KNOB_ENABLE_SWAT_WAYS;

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
  auto victim = std::find_if(begin, end, [](BLOCK x) { return x.lru == maxRRPV && x.sectorHolder.is_sector_line == false; }); // hijack the lru field

  // not found
  while (victim == end) 
  {
    // increase lru
    for (auto it = begin; it != end; ++it)
    {
      it->lru++;
    }
    // test again && make sure we dont give out sector line
    victim = std::find_if(begin, end, [](BLOCK x) { return x.lru == maxRRPV && x.sectorHolder.is_sector_line == false; });
  }

  // for SWAT, we dont setup victima_block flag so we are safe 

  // found
  int k_times = 5;
  while(k_times--)
  {
    // it is not a victima_block then return victim_candidate
    if(!victim->victima_block)
    {
      return std::distance(begin, victim);
    }

    // it is victima_block reduce its lru value so that it donest get capured again as a lru candidate
    victim->lru--;
    for (auto it = begin; it != end; ++it)
    {
      if(it == victim) continue;
      if(it->lru >= maxRRPV) continue;
      it->lru++;
    }
    victim = std::find_if(begin, end, [](BLOCK x) {return x.lru == maxRRPV;});
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
