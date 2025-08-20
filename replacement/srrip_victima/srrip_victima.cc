#include "cache.h"

#define maxRRPV 3

// initialize replacement state
void CACHE::initialize_replacement()
{
  for (auto& blk : block)
    blk.lru = maxRRPV;
}

// find replacement victim
uint32_t CACHE::find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK* current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
  // look for the maxRRPV line
  auto begin = std::next(std::begin(block), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);
  auto victim = std::find_if(begin, end, [](BLOCK x) { return x.lru == maxRRPV; }); // hijack the lru field
  auto backup_way = victim; // hijack the lru field

  int ktimes = maxRRPV+1;

  uint32_t way = 0;
  while (ktimes) {

    for (auto it = begin; it != end; ++it)
      it->lru++;
    
    victim = std::find_if(begin, end, [](BLOCK x) { return x.lru == maxRRPV; });
    if(victim != end)
    {
      if(!victim->victima_block)
      {
        uint32_t way1 = std::distance(begin, victim);
        uint32_t way2 = std::distance(begin, backup_way);
        if(way1 == NUM_WAY && way2 == NUM_WAY)
        {

        }
        else if(way1 != NUM_WAY)
        {
          way = way1;
          break;
        }
        else if(way2 != NUM_WAY)
        {
          way = way2;
          break;
        }

      }
      else backup_way = victim;
    }
    ktimes--;
  }

  

  return way;
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
