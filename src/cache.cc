#include "cache.h"

#include <algorithm>
#include <iterator>

#include "champsim.h"
#include "champsim_constants.h"
#include "util.h"
#include "vmem.h"
#include "user.h"
#include "ptw.h"
#include "pomtlb.h"

#ifndef SANITY_CHECK
#define NDEBUG
#endif

#define SHARED 3

// Extra configguration
extern int KNOB_TRANSLATION_QUEUE;
extern int KNOB_STLB_DO_NOT_TRACK_MISS;
extern int KNOB_VICTIMA, KNOB_EXTEND_VICTIMA, KNOB_HASH_CACHE_MAX_LIMIT;
extern int KNOB_SMT_ENABLE;

// illusiong of stored cache line by 8byte granularity
extern list<pair<string, uint64_t>> hash_cache;
extern int KNOB_VICTIMA, KNOB_IDEAL_VICTIMA, KNOB_POMTLB;

// illusiong of stored cache line by 8byte granularity
extern map<uint64_t, uint64_t> l2_pte_map;
extern ProcessPageTable* process_page_table;
extern ProcessPageTable* pom_page_table;
extern uint64_t POM_CPU_KEY;
extern POMTLB* pomtlb;
extern VirtualMemory vmem;
extern uint8_t warmup_complete[NUM_CPUS];

void CACHE::handle_fill()
{
  while (writes_available_this_cycle > 0) {
    auto fill_mshr = MSHR.begin();

    if(fill_mshr->thread_id==-1 && fill_mshr->type != PREFETCH)
    {
      dassert.log("handle_fill: thread_id == -1 and request != PREFETCH", "instr", fill_mshr->instr_id, "addr", intToHex(fill_mshr->address), "v_addr", intToHex(fill_mshr->v_address), "type", fill_mshr->type, "NAME", NAME, "victima", fill_mshr->vflag[VF::victima], "pom", fill_mshr->pomflag[POM::POM], "\n");
      exit(-1);
    }

    if (fill_mshr == std::end(MSHR) || fill_mshr->event_cycle > current_cycle)
    {
      cacheDataModel->mshr_queue_stalls[Stall::OP_PENALTY]++;
      return;
    }

    // debugLog.log(current_cycle, NAME, intToHex(fill_mshr->address), intToHex(fill_mshr->v_address), intToHex(fill_mshr->data), "pom", fill_mshr->pomflag[POM::POM], "pommiss", fill_mshr->pomflag[POM::POM_MISS], "pom_to_ptw", fill_mshr->pomflag[POM::POM_TO_PTW], "pom_to_ptw_fini", fill_mshr->pomflag[POM::POM_TO_PTW_FINI], '\n');

    // order matters
    // No write - hence No tracking of PTE
    // But want to track access
    // allow only POM packet to enter, we will disable it when packet state change to POM_TO_PTW to allow it to write to STLB
    if(KNOB_POMTLB && fill_mshr->pomflag[POM::POM])
    {
      if(cache_is[IS_STLB])
      {
        // if it is a returning POM_TO_PTW at STLB, then its state now FINI, test it
        // and do nothing if  POM_TO_PTW_FINI set
        if(fill_mshr->pomflag[POM::POM_TO_PTW_FINI]) {}
        // retry code: POM_TO_PTW was a miss, erase our entry and add new to RQ for default PTW
        else if(fill_mshr->pomflag[POM::POM_TO_PTW])
        {
          if(get_occupancy(1,0) == get_size(1,0))
          {
            cacheDataModel->mshr_queue_stalls[Stall::OP_PENALTY]++;
            return;
          }

          pomtlb->pom_counters[POMFLAG::POM_SECOND_REQ]++;

          // POM failed, now request for PTW
          fill_mshr->event_cycle = std::numeric_limits<uint64_t>::max();
          fill_mshr->dtype = DataType::INVALID;
          fill_mshr->hit_where = CACHE_ID_END;

          PACKET newPacket = *fill_mshr;
          
          // keeping this POM address
          newPacket.pom_address = fill_mshr->address;
          // reset POMTLBaddress to virt address
          newPacket.address = newPacket.v_address;
          // this is last step, this will allow POM_TO_PTW packet to neven re-enter
          // here again as it will become normal packet once more
          newPacket.pomflag[POM::POM] = false;

          add_rq(&newPacket);
          
          func_track_miss_access_latency(fill_mshr->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_QUEUE]);
          func_track_missfulfill_access_latency(fill_mshr->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_MSHR]);
          MSHR.erase(fill_mshr);
          writes_available_this_cycle--;
          cacheDataModel->mshr_queue[Basic::ACCESS]++;
          global_access_count++;
          cacheDataModel->mshr_queue_stalls[Stall::OP_FAIL_PENALTY]++;
          continue;
        }
      }
      // avoid to write POM_MISS packet since we dont have mapping in POMTLB and it will be a empty block hence dont write
      else if(!is_tlb && fill_mshr->pomflag[POM_MISS])
      {
        func_track_miss_access_latency(fill_mshr->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_QUEUE]);
        func_track_missfulfill_access_latency(fill_mshr->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_MSHR]);
        func_return(&*fill_mshr);
        MSHR.erase(fill_mshr);
        // showing congestion
        writes_available_this_cycle--;
        cacheDataModel->mshr_queue[Basic::ACCESS]++;
        continue;
      }
    }
    
    // TODO: Add PTW cost predictor to decide whether to insert the cacheline as victima cache block or not
    bool victima_flag = KNOB_VICTIMA && cache_is[IS_L2] && fill_mshr->vflag[VF::victima_stlbevict_ptw];

    uint64_t address_tobe_used = (victima_flag? fill_mshr->v_address: fill_mshr->address);

    // if victima block then use virt-address to find set/way
    uint32_t set = get_set(fill_mshr->type, address_tobe_used, victima_flag);

    // if it is hit implies, Transltion cache block is already there, its PTE might not be valid one thats why it brought from lower level
    uint32_t way = get_way(fill_mshr->type, address_tobe_used, set, fill_mshr->thread_id, victima_flag);

    bool hit = way < NUM_WAY;
    if(fill_mshr->type == TRANSLATION && !is_tlb)
    {
      // block was here already, but its PTE was not valid, now it is valid, hence update
      if(hit)
      {
        
      }
    }

    auto set_begin = std::next(std::begin(block), set * NUM_WAY);
    auto set_end = std::next(set_begin, NUM_WAY);
    auto first_inv = std::find_if_not(set_begin, set_end, is_valid<BLOCK>());

    // // translation block was already here, but its PTE was not valid, now it is valid, hence update same cache block
    // way = hit ? way : std::distance(set_begin, first_inv);

    if (way == NUM_WAY)
      way = impl_replacement_find_victim(fill_mshr->cpu, fill_mshr->instr_id, set, &block.data()[set * NUM_WAY], fill_mshr->ip, fill_mshr->address,
                                         fill_mshr->type);

    bool success = filllike_miss(set, way, *fill_mshr);
    if (!success)
    {
      cacheDataModel->mshr_queue_stalls[Stall::OP_FAIL_PENALTY]++;
      return;
    }

    if (way != NUM_WAY) {
      // update processed packets
      fill_mshr->data = block[set * NUM_WAY + way].data;

      for (auto ret : fill_mshr->to_return)
        ret->return_data(&(*fill_mshr));
    }
    
    func_track_workingset(fill_mshr->address);
    func_track_miss_access_latency(fill_mshr->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_QUEUE]);
    func_track_missfulfill_access_latency(fill_mshr->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_MSHR]);

    MSHR.erase(fill_mshr);
    writes_available_this_cycle--;
    
    cacheDataModel->mshr_queue[Basic::ACCESS]++;
    global_access_count++;    
  }
}

void CACHE::handle_writeback()
{
  while (writes_available_this_cycle > 0) {
    if (!WQ.has_ready())
    {
      cacheDataModel->wr_queue_stalls[Stall::OP_PENALTY]++;
      return;
    }

    // handle the oldest entry
    PACKET& handle_pkt = WQ.front();
    if(handle_pkt.thread_id==-1 && handle_pkt.type != PREFETCH)
    {
      dassert.log("handle_writeback: thread_id == -1 and request != PREFETCH", "instr", handle_pkt.instr_id, "addr", intToHex(handle_pkt.address), "v_addr", intToHex(handle_pkt.v_address), "type", handle_pkt.type, "NAME", NAME, "victima", handle_pkt.vflag[VF::victima], "pom", handle_pkt.pomflag[POM::POM], "\n");
      exit(-1);
    }

    // access cache
    uint32_t set = get_set(handle_pkt.type, handle_pkt.address, handle_pkt.vflag[VF::victima]);
    uint32_t way = get_way(handle_pkt.type, handle_pkt.address, set, handle_pkt.thread_id, handle_pkt.vflag[VF::victima]);
    
    BLOCK& fill_block = block[set * NUM_WAY + way];
    bool hit = way < NUM_WAY;

    uint64_t vp = (handle_pkt.address & ~(PAGE_SIZE-1));
    uint64_t pp = (handle_pkt.data & ~(PAGE_SIZE-1));

    if (hit) // HIT
    {
      impl_replacement_update_state(handle_pkt.cpu, set, way, fill_block.address, handle_pkt.ip, 0, handle_pkt.type, 1);

      // COLLECT STATS
      sim_hit[handle_pkt.cpu][handle_pkt.type]++;
      sim_access[handle_pkt.cpu][handle_pkt.type]++;

      // mark dirty
      fill_block.dirty = 1;
      cacheDataModel->wr_queue[Basic::HIT]++;
      cacheDataModel->cache_stat[CacheStat::Total_Write]++;

      if(fill_block.came_from_request == PREFETCH)
        prefetch_hit_histo[set*NUM_WAY+way][WRITEBACK_HIT]++;
      
    } else // MISS
    {
      bool success;
      if (handle_pkt.type == RFO && handle_pkt.to_return.empty()) 
      {
        success = readlike_miss(handle_pkt);
      } 
      else 
      {
        // find victim
        auto set_begin = std::next(std::begin(block), set * NUM_WAY);
        auto set_end = std::next(set_begin, NUM_WAY);
        auto first_inv = std::find_if_not(set_begin, set_end, is_valid<BLOCK>());
        way = std::distance(set_begin, first_inv);
        if (way == NUM_WAY)
          way = impl_replacement_find_victim(handle_pkt.cpu, handle_pkt.instr_id, set, &block.data()[set * NUM_WAY], handle_pkt.ip, handle_pkt.address,
                                             handle_pkt.type);

        success = filllike_miss(set, way, handle_pkt);
      }
      
      if (!success)
      {
        cacheDataModel->wr_queue_stalls[Stall::OP_FAIL_PENALTY]++;
        return;
      }

      cacheDataModel->wr_queue[Basic::MISS]++;
    }

    // remove this entry from WQ
    writes_available_this_cycle--;
    WQ.pop_front();
    cacheDataModel->wr_queue[Basic::ACCESS]++;
  }
}

void CACHE::handle_read()
{
  #ifdef TQ
  while (reads_available_this_cycle > 0 && KNOB_TRANSLATION_QUEUE) {
    if (!TQ.has_ready())
    {
      cacheDataModel->rd_queue_stalls[Stall::OP_PENALTY]++;
      break;
    }

    // handle the oldest entry
    PACKET& handle_pkt = TQ.front();

    // A (hopefully temporary) hack to know whether to send the evicted paddr or
    // vaddr to the prefetcher
    ever_seen_data |= (handle_pkt.v_address != handle_pkt.ip);

    uint32_t set = get_set(handle_pkt.address, handle_pkt.vflag[VF::victima]);
    uint32_t way = get_way(handle_pkt.address, set, handle_pkt.vflag[VF::victima]);

    bool hit = way < NUM_WAY;

    if(KNOB_VICTIMA && 
      cache_is[CACHE_ID::IS_L2] &&
      handle_pkt.vflag[VF::victima])
    {
      hit = block[set*NUM_WAY + way].victima_block ? true : false;
    }
      
    if (hit) // HIT
    {
      readlike_hit(set, way, handle_pkt);
      cacheDataModel->rd_queue[Basic::HIT]++;
    } 
    else 
    {
      bool success = readlike_miss(handle_pkt);

      if (!success)
      {
        cacheDataModel->rd_queue_stalls[Stall::OP_FAIL_PENALTY]++;
        break;
      }

      cacheDataModel->rd_queue[Basic::MISS]++;
    }

    // remove this entry from RQ
    TQ.pop_front();
    reads_available_this_cycle--;
    cacheDataModel->rd_queue[Basic::ACCESS]++;
  }
  #endif

  while (reads_available_this_cycle > 0) {
    if (!RQ.has_ready())
    {
      cacheDataModel->rd_queue_stalls[Stall::OP_PENALTY]++;
      return;
    }

    // handle the oldest entry
    PACKET& handle_pkt = RQ.front();
    if(handle_pkt.thread_id==-1 && handle_pkt.type != PREFETCH)
    {
      dassert.log("handle_read: thread_id == -1 and request != PREFETCH", "instr", handle_pkt.instr_id, "addr", intToHex(handle_pkt.address), "v_addr", intToHex(handle_pkt.v_address), "type", handle_pkt.type, "NAME", NAME, "victima", handle_pkt.vflag[VF::victima], "pom", handle_pkt.pomflag[POM::POM], "\n");
      exit(-1);
    }
    
    // A (hopefully temporary) hack to know whether to send the evicted paddr or
    // vaddr to the prefetcher
    ever_seen_data |= (handle_pkt.v_address != handle_pkt.ip);

    // victima lookup from stlb-->l2 uses VP as the address for indexing into L2
    uint32_t set = get_set(handle_pkt.type, handle_pkt.address, handle_pkt.vflag[VF::victima]);
    uint32_t way = get_way(handle_pkt.type, handle_pkt.address, set, handle_pkt.thread_id, handle_pkt.vflag[VF::victima]);
   
    bool hit = way < NUM_WAY;
    
    // if victima lookup is hit, then check whether valid PTE is present in block. If not, that means it is not page-faulted yet.
    BLOCK* hit_block = &block[set * NUM_WAY + way];
    if(handle_pkt.type == TRANSLATION && hit)
    {
      int offset = get_pte_offset(handle_pkt.address);
      bool is_pte_valid = hit_block->testValidity(offset);
      bitset<8> tobits(hit_block->valid_ptes);
      // debugLog.log("CACHE-HIT"+NAME, current_cycle, "level-"+to_string(handle_pkt.translation_level), "addr", intToHex(handle_pkt.address), "v_addr", intToHex(handle_pkt.v_address), "type", (int)handle_pkt.type, "cb_addr", intToHex(hit_block->address), "cb_vaddr", intToHex(hit_block->v_address), "cb_data", intToHex(hit_block->data), "level-"+to_string(hit_block->translation_level_if_pagetable_block), "pte_offset", offset, "pte_valid", is_pte_valid, "bits", tobits, "\n");
      hit = hit && is_pte_valid;
    }

    if (hit) // HIT
    {
      readlike_hit(set, way, handle_pkt);

      if(KNOB_VICTIMA && cache_is[IS_L2] && handle_pkt.vflag[VF::victima]) victima_counters[VC::L2_READ_HIT]++;
      cacheDataModel->rd_queue[Basic::HIT]++;
    } else {
      bool success = readlike_miss(handle_pkt);
      
      if (!success)
      {
        cacheDataModel->rd_queue_stalls[Stall::OP_FAIL_PENALTY]++;
        return;
      }

      if(KNOB_VICTIMA && cache_is[IS_L2] && handle_pkt.vflag[VF::victima]) victima_counters[VC::L2_READ_MISS]++;
      cacheDataModel->rd_queue[Basic::MISS]++;
    }

    // remove this entry from RQ
    RQ.pop_front();
    reads_available_this_cycle--;
    cacheDataModel->rd_queue[Basic::ACCESS]++;
  }
}

void CACHE::handle_prefetch()
{
  while (reads_available_this_cycle > 0) {
    if (!PQ.has_ready())
    {
      cacheDataModel->pf_queue_stalls[Stall::OP_PENALTY]++;
      return;
    }

    // handle the oldest entry
    PACKET& handle_pkt = PQ.front();

    handle_pkt.dtype = DataType::PRE;

    uint32_t set = get_set(handle_pkt.type, handle_pkt.address, handle_pkt.vflag[VF::victima]);
    uint32_t way = get_way(handle_pkt.type, handle_pkt.address, set, handle_pkt.thread_id, handle_pkt.vflag[VF::victima]);

    bool hit = way < NUM_WAY;

    if (hit) // HIT
    {
      readlike_hit(set, way, handle_pkt);
      cacheDataModel->pf_queue[Basic::HIT]++;
    } else {
      bool success = readlike_miss(handle_pkt);
      if (!success)
      {
        cacheDataModel->pf_queue_stalls[Stall::OP_FAIL_PENALTY]++;
        return;
      }
      cacheDataModel->pf_queue[Basic::MISS]++;
    }

    // remove this entry from PQ
    PQ.pop_front();
    reads_available_this_cycle--;
    cacheDataModel->pf_queue[Basic::ACCESS]++;
  }
}

void CACHE::readlike_hit(std::size_t set, std::size_t way, PACKET& handle_pkt)
{
  DP(if (warmup_complete[handle_pkt.cpu]) {
    std::cout << "[" << NAME << "] " << __func__ << " hit";
    std::cout << " instr_id: " << handle_pkt.instr_id << " address: " << std::hex << (handle_pkt.address >> use_offset(handle_pkt.type));
    std::cout << " full_addr: " << handle_pkt.address;
    std::cout << " full_v_addr: " << handle_pkt.v_address << std::dec;
    std::cout << " type: " << +handle_pkt.type;
    std::cout << " cycle: " << current_cycle << std::endl;
  });

  BLOCK& hit_block = block[set * NUM_WAY + way];
  hit_block.hit_before_eviction++;
  handle_pkt.hit_where = cache_id;

  if(handle_pkt.type == TRANSLATION && !is_tlb)
  {
    int offset = get_pte_offset(handle_pkt.address);
    bool is_pte_valid = hit_block.testValidity(offset);
  }

  handle_pkt.data = hit_block.data;
  if(handle_pkt.type == TRANSLATION && !is_tlb)
  {
    int cpu_id = handle_pkt.cpu * KNOB_SMT_ENABLE + handle_pkt.thread_id;
    pair<bool, PTEHolder> pte_value = process_page_table->get_pte(cpu_id, handle_pkt.address, handle_pkt.translation_level);
    // debugLog.log("readlike_hit", current_cycle, "level-"+to_string(handle_pkt.translation_level), "instr", handle_pkt.instr_id, "addr", intToHex(handle_pkt.address), "v_addr", intToHex(handle_pkt.v_address), "type", (int)handle_pkt.type, "cb_addr", intToHex(hit_block.address), "cb_vaddr", intToHex(hit_block.v_address), "cb_data", intToHex(hit_block.data), "pte_fault", !pte_value.first, "pte_value", intToHex(pte_value.second), "NAME", NAME, "\n");
    handle_pkt.data = pte_value.second.page_address;
  }

  if(handle_pkt.pomflag[POM::POM])
    

  // update prefetcher on load instruction
  if (should_activate_prefetcher(handle_pkt.type) && handle_pkt.pf_origin_level < fill_level) {
    cpu = handle_pkt.cpu;
    uint64_t pf_base_addr = (virtual_prefetch ? handle_pkt.v_address : handle_pkt.address) & ~bitmask(match_offset_bits ? 0 : use_offset(handle_pkt.type));
    handle_pkt.pf_metadata = impl_prefetcher_cache_operate(pf_base_addr, handle_pkt.ip, 1, handle_pkt.type, handle_pkt.pf_metadata);
  }

  // update replacement policy
  impl_replacement_update_state(handle_pkt.cpu, set, way, hit_block.address, handle_pkt.ip, 0, handle_pkt.type, 1);

  // COLLECT STATS
  sim_hit[handle_pkt.cpu][handle_pkt.type]++;
  sim_access[handle_pkt.cpu][handle_pkt.type]++;

  for (auto ret : handle_pkt.to_return)
    ret->return_data(&handle_pkt);

  // update prefetch stats and reset prefetch bit
  if (hit_block.prefetch) {
    pf_useful++;
    hit_block.prefetch = 0;
  }

  if(hit_block.came_from_request == PREFETCH)
    prefetch_hit_histo[set*NUM_WAY+way][READ_HIT]++;

  func_track_hit_access_latency(handle_pkt.type_cycle_enqueued[CYCLE_ENQ::TS_ADD_QUEUE], handle_pkt.vflag[VF::victima]);
}

bool CACHE::readlike_miss(PACKET& handle_pkt)
{
  if(cache_is[IS_L2])
  {
    translation_pollution->countPollution(get_set(handle_pkt.type, handle_pkt.address), 
                    PollutionEntry
                    (
                      handle_pkt.address>>(match_offset_bits?0:use_offset(handle_pkt.type)), 
                      make_pair(PollutionTracker::TranslationPollutionTracker, EvictCause::INVALID_CAUSE), 
                      handle_pkt.thread_id
                    )
                );
 
    if(handle_pkt.vflag[VF::victima])
    {
      victima_pollution->countPollution(get_set(handle_pkt.type, handle_pkt.address), 
                    PollutionEntry
                    (
                      handle_pkt.address>>(match_offset_bits?0:use_offset(handle_pkt.type)), 
                      make_pair(PollutionTracker::VictimaPollutionTracker, EvictCause::INVALID_CAUSE), 
                      handle_pkt.thread_id
                    )
                );
    }
  }

  // position matters
  // this executes for stlbmiss and lookup at L2, not for STLB eviction triggered PTW
  if(KNOB_VICTIMA && handle_pkt.vflag[VF::victima_stlbevict_ptw]==false)
  {
    if(cache_is[IS_L2] && handle_pkt.vflag[VF::victima])
    {
      // its a miss and not DP (dummy packet) hence set, AP miss
      handle_pkt.vflag[victima_acutal_packet_miss] = 1;
      for(auto ret: handle_pkt.to_return)
      {
        ret->return_data(&handle_pkt);
      }
      return true;
    }
  }

  cacheDataModel->mshr_queue[Basic::REQUESTED]++;

  DP(if (warmup_complete[handle_pkt.cpu]) {
    std::cout << "[" << NAME << "] " << __func__ << " miss";
    std::cout << " instr_id: " << handle_pkt.instr_id << " address: " << std::hex << (handle_pkt.address >> use_offset(handle_pkt.type));
    std::cout << " full_addr: " << handle_pkt.address;
    std::cout << " full_v_addr: " << handle_pkt.v_address << std::dec;
    std::cout << " type: " << +handle_pkt.type;
    std::cout << " cycle: " << current_cycle << std::endl;
  });

  // check mshr
  bool check_thread_id = is_tlb || (KNOB_VICTIMA && cache_is[IS_L2] && handle_pkt.vflag[VF::victima]);
  auto mshr_entry = std::find_if(MSHR.begin(), MSHR.end(), eq_addr<PACKET>(handle_pkt.address, use_offset(handle_pkt.type), handle_pkt.thread_id, check_thread_id));
  bool mshr_full = (MSHR.size() == MSHR_SIZE);

  // usercode
  if (mshr_entry != MSHR.end() && !(cache_is[CACHE_ID::IS_STLB] &&  KNOB_STLB_DO_NOT_TRACK_MISS)) // miss already inflight
  {
    // update fill location
    mshr_entry->fill_level = std::min(mshr_entry->fill_level, handle_pkt.fill_level);

    packet_dep_merge(mshr_entry->lq_index_depend_on_me, handle_pkt.lq_index_depend_on_me);
    packet_dep_merge(mshr_entry->sq_index_depend_on_me, handle_pkt.sq_index_depend_on_me);
    packet_dep_merge(mshr_entry->instr_depend_on_me, handle_pkt.instr_depend_on_me);
    packet_dep_merge(mshr_entry->to_return, handle_pkt.to_return);

    if (mshr_entry->type == PREFETCH && handle_pkt.type != PREFETCH) 
    {
      // Mark the prefetch as useful
      if (mshr_entry->pf_origin_level == fill_level)
        pf_useful++;

      uint64_t prior_event_cycle = mshr_entry->event_cycle;
      *mshr_entry = handle_pkt;

      // in case request is already returned, we should keep event_cycle
      mshr_entry->event_cycle = prior_event_cycle;
    }

    cacheDataModel->mshr_queue[Basic::MERGED]++;
  } 
  else 
  {
    bool is_read = prefetch_as_load || (handle_pkt.type != PREFETCH);

    // Order Really Matter
    // #1
    if (mshr_full)  // not enough MSHR resource
    {
      cacheDataModel->adv_stats[AdvStat::CASCADE_STALL_READLIKEMISS_MSHR_FULL]++;
      cacheDataModel->mshr_queue[Basic::REJECTED]++;
      return false; // TODO should we allow prefetches anyway if they will not
                    // be filled to this level?
    }

    // If victima-ideal, do send zero-latency lookup. If miss send usual packet
    if(KNOB_IDEAL_VICTIMA && KNOB_VICTIMA && cache_is[IS_STLB])
    {
      // soft lookup
      pair<bool, PTEHolder> found_peek = ((CACHE*)l2cache->getObject())->victima_peek_singleline(handle_pkt);
      if(found_peek.first)
      {
        handle_pkt.data = found_peek.second.page_address;
        for(auto ret: handle_pkt.to_return)
          ret->return_data(&handle_pkt);
        
        func_track_hit_access_latency(handle_pkt.type_cycle_enqueued[CYCLE_ENQ::TS_ADD_QUEUE]);
        return true;
      }
    }

    bool sendVictimaPacket = KNOB_VICTIMA && cache_is[IS_STLB] && KNOB_IDEAL_VICTIMA==0;
    // its a miss and we are @STLB and its not yet has searched POMTLB, send it to POM search via L1D
    bool sendPomPacket = KNOB_POMTLB && cache_is[IS_STLB] && !handle_pkt.pomflag[POM::POM_TO_PTW];
    // if(cache_id == CACHE_ID::IS_STLB) cout << "Log: " << KNOB_POMTLB << ", " << (!handle_pkt.pomflag[POM::POM_TO_PTW]) << ", " << (handle_pkt.type==TRANSLATION) << '\n';

    // Test Occupancy of L2, since victima packet parallely sends PTW packet request we need to test PTW rq occupancy
    if(sendVictimaPacket)
    {
      if(l2cache->get_occupancy(1,0) == l2cache->get_size(1,0))
      {
        return false;
      }
    }
    // Test Occupancy of L1, we dont sent PTW packet until POM packet comes back with a miss at POMTLB
    else if(sendPomPacket)
    {
      if(l2cache->get_occupancy(1,0) == l2cache->get_size(1,0))
      {
        return false;
      }
    }
    // Default-code: Test Occupancy of lower level
    else
    {
      // check to make sure the lower level queue has room for this read miss
      int queue_type = (is_read) ? 1 : 3;
      if (lower_level->get_occupancy(queue_type, handle_pkt.address) == lower_level->get_size(queue_type, handle_pkt.address))
      {
        cacheDataModel->adv_stats[AdvStat::CASCADE_STALL_READLIKEMISS_NEXTLEVEL_FULL]++;
        return false;
      }
    }
    
    PACKET newPacket = handle_pkt;
    if(sendVictimaPacket)
    {
      newPacket.address = handle_pkt.address;
      newPacket.v_address = handle_pkt.v_address;
      newPacket.to_return = {this};
      newPacket.vflag[VF::victima] = true;
      newPacket.thread_id = handle_pkt.thread_id;

      // soft lookup
      pair<bool, PTEHolder> found_peek = ((CACHE*)l2cache->getObject())->victima_peek_singleline(newPacket);

      // to fix difference in returned physical address ex. F: 346681344, S: 4641652728
      // do softlookup, if not in cache then set dumy status to simulate traffic and dont use its results.
      if(found_peek.first)
      {
        newPacket.vflag[VF::victima_dumy] = false;
        newPacket.vflag[VF::PACKET_AP_RECV] = true;
        newPacket.vflag[VF::PACKET_DP_RECV] = false;
      }
      else
      {
        newPacket.vflag[VF::victima_dumy] = true;
        newPacket.vflag[VF::PACKET_AP_RECV] = false;
        newPacket.vflag[VF::PACKET_DP_RECV] = true;
      }

      // if victima_block @L2 has PTE then send dumy to PTW
      handle_pkt.vflag[VF::victima_dumy] = !newPacket.vflag[VF::victima_dumy];
      handle_pkt.vflag[VF::PACKET_AP_RECV] = !newPacket.vflag[VF::PACKET_AP_RECV];
      handle_pkt.vflag[VF::PACKET_DP_RECV] = !newPacket.vflag[VF::PACKET_DP_RECV];
      handle_pkt.vflag[VF::ptw_copy] = true;
    }
    else if(sendPomPacket)
    {
      // generate phy address for POMTLB
      PageTableWalker* ptw = (PageTableWalker*)lower_level->getObject();
      uint64_t pomtlb_base = ptw->get_pomtlb_baseaddr();
      pomtlb_base += (handle_pkt.address ^ ptw->asid[handle_pkt.thread_id]);
      // update address to pomtlb address
      handle_pkt.address = pomtlb_base;
      int cpu_id = cpu * KNOB_SMT_ENABLE + handle_pkt.thread_id;
      handle_pkt.pomflag[POM::POM] = true;

      // use newpacket so that we will return it till STLB not futher like D/I +TLB
      newPacket = handle_pkt;
      // make sure to clear earlier destination like D/I +TLB
      newPacket.to_return = {this};

      pomtlb->pom_counters[POMFLAG::POM_FIRST_REQ]++;
    }

    // Allocate an MSHR
    if (handle_pkt.fill_level <= fill_level  && !(cache_is[CACHE_ID::IS_STLB] &&  KNOB_STLB_DO_NOT_TRACK_MISS)) {

      auto it = MSHR.insert(std::end(MSHR), handle_pkt);
      it->cycle_enqueued = current_cycle;
      it->event_cycle = std::numeric_limits<uint64_t>::max();
      it->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_MSHR] = current_cycle;

      cacheDataModel->mshr_queue[Basic::ADDED]++;

      // placed here making sure MSHR entry is first inserted. The reason is it might receive hit in WQ of L2, that time it will
      // try to return data to STLB and wont find MSHR hence to prevent such situtation
      if(sendVictimaPacket)
      {
        int status = l2cache->add_rq(&newPacket);
      }
    }

    if( !(cache_is[CACHE_ID::IS_STLB] &&  KNOB_STLB_DO_NOT_TRACK_MISS))
    {
      if (handle_pkt.fill_level <= fill_level)
        handle_pkt.to_return = {this};
      else
        handle_pkt.to_return.clear();
    }

    //POMTLB: record miss, send to cache-hierarchy + No PTW requests, PTW start upon POM return.
    if(sendPomPacket)
    {
      dlog.log(current_cycle, NAME, "Send POM", intToHex(handle_pkt.address), intToHex(handle_pkt.v_address), handle_pkt.pomflag[POM::POM], handle_pkt.pomflag[POM::POM_MISS], handle_pkt.pomflag[POM::POM_TO_PTW], '\n');

      l1cache->add_rq(&newPacket);
    }
    else if (!is_read)
    {
      lower_level->add_pq(&handle_pkt);
    }
    else
    {
      lower_level->add_rq(&handle_pkt);
    }
  }

  // update prefetcher on load instructions and prefetches from upper levels
  if (should_activate_prefetcher(handle_pkt.type) && handle_pkt.pf_origin_level < fill_level) {
    cpu = handle_pkt.cpu;
    uint64_t pf_base_addr = (virtual_prefetch ? handle_pkt.v_address : handle_pkt.address) & ~bitmask(match_offset_bits ? 0 : use_offset(handle_pkt.type));
    handle_pkt.pf_metadata = impl_prefetcher_cache_operate(pf_base_addr, handle_pkt.ip, 0, handle_pkt.type, handle_pkt.pf_metadata);
  }
  
  //check if reuse_history has tracked this miss
  uint32_t set = get_set(handle_pkt.type, handle_pkt.address, handle_pkt.vflag[VF::victima]);
  uint32_t way = get_way(handle_pkt.type, handle_pkt.address, set, handle_pkt.vflag[VF::victima]);
  uint64_t target_addr = handle_pkt.address;
  auto it = std::find_if(reuse_history[set].begin(), reuse_history[set].end(), eq_addr<BLOCK>(target_addr, use_offset(handle_pkt.type)));
  if(it!=reuse_history[set].end())
  {
    int dist = std::distance(reuse_history[set].begin(), it);
    cacheDataModel->hist_reuse_distance[dist]++;
  }

  uint64_t tag = target_addr & ~((1 << (LOG2_BLOCK_SIZE + lg2(NUM_SET))) - 1);
  if(is_tlb)
    tag = target_addr & ~(PAGE_SIZE-1);
  
  auto g_it = global_reuse.find(tag);
  if(global_reuse.end() != g_it)
  {
    uint64_t last_global_access = g_it->second;
    int distance = global_access_count > last_global_access? (global_access_count - last_global_access): 0;
    cacheDataModel->reuse_distance->add_data_freq(distance, 1);
  }
  global_reuse[tag] = global_access_count;

  return true;
}

bool CACHE::filllike_miss(std::size_t set, std::size_t way, PACKET& handle_pkt)
{
  BLOCK& fill_block = block[set * NUM_WAY + way];

  // Position matters
  // POM packet return mem trip. Test if it was hit in POM-TLB. If so, return data (from handle_fill)
  // If not hit, then discard this fill request, initiate PTW
  // Note: since we have removed RQ entry for this packet at STLB,
  // We could remove MSHR entry here and add new entry to RQ of STLB --> (detailed modeling)
  // Right now we are using same MSHR entry and initiate PTW by setting new flag POM_TO_PTW
  // As soon we are back to STLB, we send out PTW with POM_TO_PTW flag. 
  if(KNOB_POMTLB && cache_is[IS_STLB] && handle_pkt.pomflag[POM::POM])
  {
    // remove this MSHR, add new request to STLB RQ with status POM_TO_PTW to avoid another POM request but prefer PTW request this time 
    if(handle_pkt.pomflag[POM::POM_MISS])
    {
      handle_pkt.pomflag[POM::POM_MISS] = false;
      // We wont remove this MSHR and reuse this to send out PTW and then reset its event_cycle to avoid re-entering to fill
      handle_pkt.pomflag[POM::POM_TO_PTW] = true;
      return false;
    }

    pomtlb->pom_counters[POMFLAG::POM_SUCCESS]++;
  }
  
  // // Test POM Lookup
  // if(cache_id == CACHE_ID::IS_STLB && handle_pkt.pomflag[POM::POM_TO_PTW])
  // {
  //   PageTableWalker* ptw = (PageTableWalker*)lower_level->getObject();
  //   uint64_t pomtlb_base = ptw->get_pomtlb_baseaddr();
  //   pomtlb_base += (handle_pkt.address ^ ptw->asid[handle_pkt.thread_id]);
  //   // update address to pomtlb address
  //   int cpu_id = KNOB_SMT_ENABLE * handle_pkt.cpu + handle_pkt.thread_id;
    
  //     // probe POMTLB
  //     auto[ flag, addr] = pomtlb->lookupPOMEntry(cpu_id, pomtlb_base, handle_pkt.v_address);
  //     cout << "POM LOOKUP Status, POM-addr, " << intToHex(page_align(pomtlb_base)) << ", V-addr, "<< intToHex(page_align(handle_pkt.v_address)) <<", result, "<< flag <<", data, "<< intToHex(addr) <<", packet_data, " << intToHex(page_align(handle_pkt.data)) << '\n';
  // }

  // Block is valid, we are dropping it from STLB
  // test if we do have have this costly block as Victima-block available in L2-cache, if not then test can we add PTW request
  bool sendVictimaPTWRequest = 0;
  // Proposed-idea: hashcache full then  do remove one hash entry and with that we need to remove corrresponding L2 cache-block
  // order of uasge of variable matter: writeExtendedVictimaPacket is our goal, and adjustHashCache is a management
  bool adjustHashCache = 0;
  bool writeExtendedVictimaPacket = 0;
  if(cache_is[IS_STLB] && fill_block.valid)
  {
    if(KNOB_EXTEND_VICTIMA)
    {
      // // IF GAIN IS NOT AVAILABLE THEN DONT USE THIS MAINTAINANCE CODE
      // // test hashcache size: we will send invalidation packet to L2 if entry hash_cache is full: invalidationPacket
      // if(hash_cache.size() >= KNOB_HASH_CACHE_MAX_LIMIT)
      // {
      //   // test L2 read queue occupancy: if full we cannot send invalidation packet to 
      //   if(l2cache->get_occupancy(1,0) == l2cache->get_size(1,0))
      //   {
      //     return false;
      //   }
      //   adjustHashCache = 1;
      // }

      // test if cluster-8 available to be able to written in L2
      PACKET packet;
      packet.thread_id = handle_pkt.thread_id;
      pair<bool, PTEContainer> ret = collect_pte[packet.thread_id].test();

      // cluster-8 is avail
      if(ret.first)
      {
        /// test WQ occupancy at L2
        if(l2cache->get_occupancy(2,0) == l2cache->get_size(2,0))
        {
          return false;
        }
        writeExtendedVictimaPacket = 1;
      }
    }
    else if(KNOB_VICTIMA)
    {
      // // initiate PTW when RQ has occupancy & TLB block is absent
      if(lower_level->get_occupancy(1,0) != lower_level->get_size(1,0))
      {
        // refet PTW-CP
        int cpu_id = (handle_pkt.cpu * KNOB_SMT_ENABLE + handle_pkt.thread_id);
        sendVictimaPTWRequest = victima_lookup(handle_pkt.v_address, cpu_id);
      }
    }
  }

  DP(if (warmup_complete[handle_pkt.cpu]) {
    std::cout << "[" << NAME << "] " << __func__ << " miss";
    std::cout << " instr_id: " << handle_pkt.instr_id << " address: " << std::hex << (handle_pkt.address >> use_offset(handle_pkt.type));
    std::cout << " full_addr: " << handle_pkt.address;
    std::cout << " full_v_addr: " << handle_pkt.v_address << std::dec;
    std::cout << " type: " << +handle_pkt.type;
    std::cout << " cycle: " << current_cycle << std::endl;
  });

  bool bypass = (way == NUM_WAY);
#ifndef LLC_BYPASS
  assert(!bypass);
#endif
  assert(handle_pkt.type != WRITEBACK || !bypass);
  bool evicting_dirty = !bypass && (lower_level != NULL) && fill_block.dirty;

  // since writebacks from i/dtlb are making cache-blocks at STLB dirty, 
  // it is by default getting writeback to PTW (which is wrong hence making it explitcitly evicting=false)
  if(KNOB_STLB_DO_NOT_TRACK_MISS)
  {
    if(cache_is[CACHE_ID::IS_DTLB] || cache_is[CACHE_ID::IS_ITLB])
      evicting_dirty = 1;
    else if(cache_is[CACHE_ID::IS_STLB])
      evicting_dirty = 0;
  }

  uint64_t evicting_address = 0;

  if (!bypass) {
    if (evicting_dirty) 
    {
      PACKET writeback_packet;

      writeback_packet.fill_level = lower_level->fill_level;
      writeback_packet.cpu = handle_pkt.cpu;
      writeback_packet.address = fill_block.address;
      writeback_packet.data = fill_block.data;
      writeback_packet.instr_id = handle_pkt.instr_id;
      writeback_packet.ip = 0;
      writeback_packet.type = WRITEBACK;
      writeback_packet.thread_id = fill_block.thread_id;

      auto result = lower_level->add_wq(&writeback_packet);
      if (result == -2)
      {
        cacheDataModel->adv_stats[AdvStat::CASCADE_STALL_FILLLIKEMISS_NEXTLEVEL_FULL]++;
        return false;
      }
    }
   
    bool track_reuse = false;
   
    // invalid block
    // count Compulsory miss
    if(!fill_block.valid)
    {  
      cacheDataModel->category_of_misses[MISS::COM]++;
    }
    // check for conflict misses && capacity misses
    // valid blocks (may be dirty or clean)
    else
    {
      if(KNOB_VICTIMA)
      {
        if(cache_is[CACHE_ID::IS_STLB])
        {
          // Victima Routine
          // TLB block is absent at L2
          if(sendVictimaPTWRequest)
          {
            // Sending evicted packet for PTW

            PACKET ptwpacket;
            ptwpacket.to_return = {};
            ptwpacket.cpu = fill_block.cpu;
            ptwpacket.address = fill_block.v_address;
            ptwpacket.v_address = fill_block.v_address;
            ptwpacket.data = fill_block.data;
            ptwpacket.instr_id = fill_block.instr_id;
            ptwpacket.ip = 0;
            ptwpacket.type = TRANSLATION;
            ptwpacket.vflag[VF::victima_stlbevict_ptw] = true;//storing result of leaf-pte to L2 and transforming it to TLBblock or victimablock
            ptwpacket.thread_id = fill_block.thread_id;

            lower_level->add_rq(&ptwpacket);
            victima_counters[VC::VICTIMA_PTW_COUNT]++;
          }
          // Extended-Victima Routine
          else if(writeExtendedVictimaPacket)
          {
            PACKET writeback_packet;

            writeback_packet.fill_level = l2cache->fill_level;
            writeback_packet.cpu = handle_pkt.cpu;
            writeback_packet.address = fill_block.address;
            writeback_packet.data = fill_block.data;
            writeback_packet.instr_id = handle_pkt.instr_id;
            writeback_packet.ip = 0;
            writeback_packet.type = WRITEBACK;
            writeback_packet.vflag[VF::victima] = true;
            writeback_packet.thread_id = handle_pkt.thread_id;

            // order matters
            {
              // test hashcache size: we will send invalidation packet to L2 if entry hash_cache is full 
              if(adjustHashCache)
              {
                // make space in hash_cache, sends invalid packet to L2 via add_rq()
                adjust_hashcache();
              }
            }

            // check if cluster is avail to be written at L2
            if(use_cluster(&writeback_packet))
            {
              l2cache->add_wq(&writeback_packet);
            }
            
            // add packet to relevant cluster if space is available; otherwise just write it as normal victima or leave it?
            if(add_to_cluster(&writeback_packet) == -1)// failed
            {

            }
            // if packet added to cluster then write will be done when proper cluster is formed later in the process// success
            else
            {
              
            }
          }

          // tracking
          func_track_evicted_pte(handle_pkt.v_address, fill_block.data);
          victima_counters[VC::STLB_EVICT]++;
        }
        // if victima block from L2 is evicted
        else if(cache_is[CACHE_ID::IS_L2] && fill_block.victima_block)
        {
          int cpu_id = (fill_block.cpu * KNOB_SMT_ENABLE + fill_block.thread_id);
          int usage = process_page_table->get_cacheblock_usage(cpu_id, fill_block.original_pagetable_cacheblock_address, fill_block.translation_level_if_pagetable_block, "victima_evict-->filllikemiss").first;
          victima_block_usage[usage]++;
          victima_counters[VC::L2_EVICT]++;
        }
        
        if(cache_is[IS_L2])
        {
          // track victima_fill
          if(handle_pkt.vflag[VF::victima_stlbevict_ptw])
            victima_counters[VC::L2_WRITE]++;

          // track pollution for victima
          uint64_t track_addr = fill_block.address >> (match_offset_bits ? 0 : use_offset(handle_pkt.type));
          EvictCause evict_cause = (handle_pkt.vflag[VF::victima_stlbevict_ptw] && fill_block.dtype == DataType::DATA) ? (EvictCause::DATA_BLOCK_EVICTED_BY_TRANSLATION_BLOCK) : (EvictCause::INVALID_CAUSE);
          victima_pollution->insert(set, PollutionEntry(track_addr, make_pair(PollutionTracker::VictimaPollutionTracker, evict_cause), fill_block.thread_id));
        }
      }

      // track pollution && Test page table for being evicted cache_block
      {
        // @ data cache
        if(cache_is[IS_L2])
        {
          uint64_t track_addr = fill_block.address >> (match_offset_bits ? 0 : use_offset(handle_pkt.type));
          EvictCause evict_cause = (handle_pkt.type == TRANSLATION && fill_block.dtype == DataType::DATA) ? (EvictCause::DATA_BLOCK_EVICTED_BY_TRANSLATION_BLOCK) : (EvictCause::INVALID_CAUSE);
          translation_pollution->insert(set, PollutionEntry(track_addr, make_pair(PollutionTracker::TranslationPollutionTracker, evict_cause), fill_block.thread_id));
        }
      }

      //// TO TEST if PAGETBALE is OK
      // if(fill_block.came_from_request == TRANSLATION)
      // {
      //   func_test_page_table(fill_block);
      // }

      // counting the number of times set has seen conflict and as a result a dirty block is sent-back
      // it needs infinit FA cache to keep history
      // cacheDataModel->category_of_misses[MISS::CAP]++;
      // count Capacity misses
      uint64_t page = handle_pkt.address & ~(PAGE_SIZE-1);
      auto page_it = page_to_block.find(page);
      if(page_it!=page_to_block.end())
      {
        if(is_tlb)
        {
          cacheDataModel->category_of_misses[MISS::CAP]++;
        }
        else
        {
          uint64_t cache_block_index = (handle_pkt.address > 6) & 0x3f;
          if(page_it->second.test(cache_block_index))
            cacheDataModel->category_of_misses[MISS::CAP]++;
        }
      }

      // count Conflict misses
      {
        auto it = std::find_if(fa_array.begin(), fa_array.end(), eq_addr<BLOCK>(handle_pkt.address, use_offset(handle_pkt.type), handle_pkt.thread_id, is_tlb));
        if(it!=fa_array.end())
        {
          cacheDataModel->category_of_misses[MISS::CONF]++;
        }
      }
      
      if(evicting_dirty)
      {
        if(handle_pkt.type == LOAD)
          cacheDataModel->cache_stat[CacheStat::Load_Writeback]++;
        else if(handle_pkt.type == TRANSLATION)
          cacheDataModel->cache_stat[CacheStat::Translation_Writeback]++;
        else if(handle_pkt.type == RFO)
          cacheDataModel->cache_stat[CacheStat::RFO_Writeback]++;
        else if(handle_pkt.type == PREFETCH)
          cacheDataModel->cache_stat[CacheStat::Prefetch_Writeback]++;
        
        cacheDataModel->cache_stat[CacheStat::Total_Writeback]++;
      }
      else
      {
        // tracking type of cache block being dropped
        if(handle_pkt.type == LOAD)
          cacheDataModel->cache_stat[CacheStat::Load_Drop]++;
        else if(handle_pkt.type == TRANSLATION)
          cacheDataModel->cache_stat[CacheStat::Translation_Drop]++;
        else if(handle_pkt.type == RFO)
          cacheDataModel->cache_stat[CacheStat::RFO_Drop]++;
        else if(handle_pkt.type == PREFETCH)
          cacheDataModel->cache_stat[CacheStat::Prefetch_Drop]++;
        cacheDataModel->cache_stat[CacheStat::Total_Drop]++;
      }

      // track evicted/overwritten block
      {
        if(fa_array.size() >= FA_SIZE)
        fa_array.pop_back();
        
        auto found_out = find_if(fa_array.begin(), fa_array.end(), eq_addr<BLOCK>(fill_block.address,  match_offset_bits ? 0 : use_offset(handle_pkt.type), fill_block.thread_id, is_tlb));
        if(found_out==fa_array.end())
        {
          fa_array.push_back(block[set*NUM_WAY + way]);
        }
      }

      // counting the number of times set has seen conflict and as a result a clean block is overwritten
      cacheDataModel->hist_set_conflict_events[set]++;
      track_reuse = true;
    }

    if(track_reuse)
    {
      if(reuse_history[set].size() >= 4*NUM_WAY)
        reuse_history[set].pop_front();
      else 
        reuse_history[set].push_back(block[set*NUM_WAY + way]);
    }

    if (ever_seen_data)
      evicting_address = fill_block.address & ~bitmask(match_offset_bits ? 0 : use_offset(handle_pkt.type));
    else
      evicting_address = fill_block.v_address & ~bitmask(match_offset_bits ? 0 : use_offset(handle_pkt.type));

    if (fill_block.prefetch)
      pf_useless++;

    if (handle_pkt.type == PREFETCH)
      pf_fill++;
  
    fill_block.valid = true;
    fill_block.prefetch = (handle_pkt.type == PREFETCH && handle_pkt.pf_origin_level == fill_level);
    fill_block.dirty = (handle_pkt.type == WRITEBACK || (handle_pkt.type == RFO && handle_pkt.to_return.empty()));

    // Transform cacheblock to victima cache block if it is brought in by victima PTW
    fill_block.address = (KNOB_VICTIMA && handle_pkt.vflag[VF::victima_stlbevict_ptw]) ? handle_pkt.v_address : handle_pkt.address;
    // Keep the original physical address together so that we can use it for our trick to lookup into Page Table to get all 8 PTE if needed by
    // future STLB miss and hit on this victima cache block
    fill_block.original_pagetable_cacheblock_address = handle_pkt.address;

    fill_block.v_address = handle_pkt.v_address;
    fill_block.data = handle_pkt.data;
    fill_block.ip = handle_pkt.ip;
    fill_block.cpu = handle_pkt.cpu;
    fill_block.instr_id = handle_pkt.instr_id;
    fill_block.came_from_request = handle_pkt.type;

    // Identifies block is brought in by victima PTW
    fill_block.victima_block = handle_pkt.vflag[VF::victima_stlbevict_ptw];

    fill_block.thread_id = handle_pkt.thread_id;
    fill_block.dtype = handle_pkt.dtype;
    fill_block.vp_2_pp_map.clear();
    fill_block.translation_level_if_pagetable_block = (handle_pkt.type == TRANSLATION) ? handle_pkt.translation_level: -1;

    // // To verify victima_lookup is working
    // if(cache_id == CACHE_ID::IS_L2 && handle_pkt.type == TRANSLATION && handle_pkt.translation_level == 1 && handle_pkt.vflag[VF::victima_stlbevict_ptw])
    // {
    //   debugLog.log(current_cycle, "fill-cache", NAME, "addr", intToHex(handle_pkt.address), "v_addr", handle_pkt.v_address, "instr", handle_pkt.instr_id, "return_size", handle_pkt.to_return.size(), "data", handle_pkt.data, "level", (int)handle_pkt.translation_level, "set", set, "way", way, '\n');
    //   debugLog.log("Testing....\n");
    //   auto[first_it, second_it] = victima_peek_singleline(handle_pkt);
    //   if(first_it)
    //   debugLog.log("Eval", "status", first_it, "pte", intToHex(second_it.page_address), '\n');
    // }

    // track the valid bits of each of PTE whenever a cache block corresponding to PT is brought in
    if(handle_pkt.type == TRANSLATION && !is_tlb)
    {
      int cpu_id = (handle_pkt.cpu * KNOB_SMT_ENABLE + handle_pkt.thread_id);
      auto pt_meta = process_page_table->get_cacheblock_usage(cpu_id, handle_pkt.address, handle_pkt.translation_level, "filllike_miss");
      fill_block.valid_ptes = pt_meta.second;
      if(pt_meta.first != __builtin_popcount(pt_meta.second))
      {
        dassert.log("Error: pte_count != valid_pte_bits", pt_meta.first, pt_meta.second, '\n');
        exit(-1);
      }
    }
  }

  if (warmup_complete[handle_pkt.cpu] && (handle_pkt.cycle_enqueued != 0))
    total_miss_latency += current_cycle - handle_pkt.cycle_enqueued;

  // update prefetcher
  cpu = handle_pkt.cpu;
  handle_pkt.pf_metadata =
      impl_prefetcher_cache_fill((virtual_prefetch ? handle_pkt.v_address : handle_pkt.address) & ~bitmask(match_offset_bits ? 0 : use_offset(handle_pkt.type)), set, way,
                                 handle_pkt.type == PREFETCH, evicting_address, handle_pkt.pf_metadata);

  // update replacement policy
  impl_replacement_update_state(handle_pkt.cpu, set, way, handle_pkt.address, handle_pkt.ip, 0, handle_pkt.type, 0);

  // COLLECT STATS
  sim_miss[handle_pkt.cpu][handle_pkt.type]++;
  sim_access[handle_pkt.cpu][handle_pkt.type]++;

  if(handle_pkt.type == LOAD)
    cacheDataModel->cache_stat[CacheStat::Load_Write]++;
  else if(handle_pkt.type == TRANSLATION)
    cacheDataModel->cache_stat[CacheStat::Translation_Write]++;
  else if(handle_pkt.type == RFO)
    cacheDataModel->cache_stat[CacheStat::RFO_Write]++;
  else if(handle_pkt.type == PREFETCH)
    cacheDataModel->cache_stat[CacheStat::Prefetch_Write]++;
  
  cacheDataModel->cache_stat[CacheStat::Total_Write]++;

  if(!is_tlb)
    cacheDataModel->block_type_counters[fill_block.dtype]++;

  return true;
}

void CACHE::operate()
{
  operate_writes();
  operate_reads();

  impl_prefetcher_cycle_operate();
}

void CACHE::operate_writes()
{
  // perform all writes
  writes_available_this_cycle = MAX_WRITE;
  handle_fill();
  handle_writeback();

  WQ.operate();
}

void CACHE::operate_reads()
{
  // perform all reads
  reads_available_this_cycle = MAX_READ;
  handle_read();
  va_translate_prefetches();
  handle_prefetch();

  TQ.operate();
  RQ.operate();
  PQ.operate();
  VAPQ.operate();
}

uint32_t CACHE::get_set(int type, uint64_t address, bool victima) 
{
  int offset = use_offset(type);
  if(KNOB_VICTIMA && victima && cache_is[IS_L2])
  {
    offset = LOG2_PAGE_SIZE + 3;//3;
  }
  return ((address >> offset) & bitmask(lg2(NUM_SET)));
}

//  |----- TAG/Page Number --------|
//  |------EXTRA------|---PTEO(3b)---|----SET----|---BO(3b)---|
uint32_t CACHE::get_way(int type, uint64_t address, uint32_t set, int th, bool victima)
{
  int offset = use_offset(type);

  // For indexing using VirtualPage address
  if(KNOB_VICTIMA && victima && cache_is[IS_L2])
  {
    // we need page offset hence
    offset = LOG2_PAGE_SIZE + 3;//lg2(NUM_SET) + 3;
  }
  // else if(type == TRANSLATION)
  // {
  //   // 8x 8byte entries in cache block
  //   offset = 3;
  // }

  auto begin = std::next(block.begin(), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);
  return std::distance(begin, std::find_if(begin, end, eq_addr<BLOCK>(address, offset, th, (is_tlb || (cache_is[IS_L2]&&victima)) )));
}

uint64_t CACHE::use_offset(int type)
{
  uint64_t offset = OFFSET_BITS;
  // if(type == TRANSLATION)
  // {
  //   offset = 3;
  // }
  return offset;
}

int CACHE::invalidate_entry(uint64_t inval_addr)
{
  uint32_t set = get_set(-1, inval_addr);
  uint32_t way = get_way(-1, inval_addr, set, -1);

  if (way < NUM_WAY)
  {
    block[set * NUM_WAY + way].valid = 0;
    block[set * NUM_WAY + way].address = 0;
  }

  return way;
}

int CACHE::add_rq(PACKET* packet)
{
  #ifdef TQ
  if(KNOB_TRANSLATION_QUEUE && packet->type == TRANSLATION)
  {
    champsim::delay_queue<PACKET>::iterator found_wq = std::find_if(WQ.begin(), WQ.end(), eq_addr<PACKET>(packet->address, match_offset_bits ? 0 : OFFSET_BITS));
    if (found_wq != WQ.end()) {
      DP(if (warmup_complete[packet->cpu]) std::cout << " MERGED_WQ" << std::endl;)
      packet->data = found_wq->data;
      for (auto ret : packet->to_return)
        ret->return_data(packet);
      WQ_FORWARD++;
      cacheDataModel->rd_queue[Basic::WQ_FWD]++;
      return -1;
    }

    // check for duplicates in the read queue
    auto found_rq = std::find_if(TQ.begin(), TQ.end(), eq_addr<PACKET>(packet->address, OFFSET_BITS));
    if (found_rq != TQ.end()) {
      DP(if (warmup_complete[packet->cpu]) std::cout << " MERGED_RQ" << std::endl;)
      packet_dep_merge(found_rq->lq_index_depend_on_me, packet->lq_index_depend_on_me);
      packet_dep_merge(found_rq->sq_index_depend_on_me, packet->sq_index_depend_on_me);
      packet_dep_merge(found_rq->instr_depend_on_me, packet->instr_depend_on_me);
      packet_dep_merge(found_rq->to_return, packet->to_return);
      RQ_MERGED++;
      cacheDataModel->rd_queue[Basic::MERGED]++;
      return 0; // merged index
    }

    // check occupancy
    if (TQ.full()) {
      RQ_FULL++;
      DP(if (warmup_complete[packet->cpu]) std::cout << " FULL" << std::endl;)
      cacheDataModel->rd_queue[Basic::REJECTED]++;
      return -2; // cannot handle this request
    }

    // if there is no duplicate, add it to RQ
    if (warmup_complete[cpu])
      TQ.push_back(*packet);
    else
      TQ.push_back_ready(*packet);

    DP(if (warmup_complete[packet->cpu]) std::cout << " ADDED" << std::endl;)

    RQ_TO_CACHE++;
    return TQ.occupancy();
  }
  #endif

  cacheDataModel->rd_queue[Basic::REQUESTED]++;
  // assert(packet->address != 0);
  if(packet->address == 0)
  {
    dassert.log("add_rq Address Zero Packet", "instr", packet->instr_id, "addr", intToHex(packet->address), "v_addr", intToHex(packet->v_address), "type", (int)packet->type, "NAME", NAME, "victima", packet->vflag[VF::victima], "pom", packet->pomflag[POM::POM], "\n");
    exit(-1);
  }
  RQ_ACCESS++;

  DP(if (warmup_complete[packet->cpu]) {
    std::cout << "[" << NAME << "_RQ] " << __func__ << " instr_id: " << packet->instr_id << " address: " << std::hex << (packet->address >> use_offset(packet->type));
    std::cout << " full_addr: " << packet->address << " v_address: " << packet->v_address << std::dec << " type: " << +packet->type
              << " occupancy: " << RQ.occupancy();
  })

  bool check_thread_id = is_tlb || (KNOB_VICTIMA && cache_is[IS_L2] && packet->vflag[VF::victima]);

  // check for the latest writebacks in the write queue
  champsim::delay_queue<PACKET>::iterator found_wq = std::find_if(WQ.begin(), WQ.end(), eq_addr<PACKET>(packet->address, match_offset_bits ? 0 : use_offset(packet->type), packet->thread_id, check_thread_id) );
  
  if (found_wq != WQ.end()) {
    DP(if (warmup_complete[packet->cpu]) std::cout << " MERGED_WQ" << std::endl;)
    packet->hit_where = CACHE_ID::WQ;
    packet->data = found_wq->data;
    for (auto ret : packet->to_return)
      ret->return_data(packet);

    WQ_FORWARD++;

    cacheDataModel->rd_queue[Basic::WQ_FWD]++;
    return -1;
  }

  // check for duplicates in the read queue
  auto found_rq = std::find_if(RQ.begin(), RQ.end(), eq_addr<PACKET>(packet->address, use_offset(packet->type), packet->thread_id, is_tlb || check_thread_id) );
  if (found_rq != RQ.end()) {
    DP(if (warmup_complete[packet->cpu]) std::cout << " MERGED_RQ" << std::endl;)

    packet_dep_merge(found_rq->lq_index_depend_on_me, packet->lq_index_depend_on_me);
    packet_dep_merge(found_rq->sq_index_depend_on_me, packet->sq_index_depend_on_me);
    packet_dep_merge(found_rq->instr_depend_on_me, packet->instr_depend_on_me);
    packet_dep_merge(found_rq->to_return, packet->to_return);

    RQ_MERGED++;

    cacheDataModel->rd_queue[Basic::MERGED]++;
    return 0; // merged index
  }

  // check occupancy
  if (RQ.full()) {
    RQ_FULL++;

    DP(if (warmup_complete[packet->cpu]) std::cout << " FULL" << std::endl;)

    cacheDataModel->rd_queue[Basic::REJECTED]++;
    return -2; // cannot handle this request
  }

  // track cycle stamp
  packet->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_QUEUE] = current_cycle;

  // if there is no duplicate, add it to RQ
  if (warmup_complete[cpu])
    RQ.push_back(*packet);
  else
    RQ.push_back_ready(*packet);

  DP(if (warmup_complete[packet->cpu]) std::cout << " ADDED" << std::endl;)

  RQ_TO_CACHE++;
    
  cacheDataModel->rd_queue[Basic::ADDED]++;
  // cacheDataModel->rd_queue[Basic::ACCESS]++;
  return RQ.occupancy();
}

int CACHE::add_wq(PACKET* packet)
{
  cacheDataModel->wr_queue[Basic::REQUESTED]++;
  WQ_ACCESS++;

  DP(if (warmup_complete[packet->cpu]) {
    std::cout << "[" << NAME << "_WQ] " << __func__ << " instr_id: " << packet->instr_id << " address: " << std::hex << (packet->address >> use_offset(packet->type));
    std::cout << " full_addr: " << packet->address << " v_address: " << packet->v_address << std::dec << " type: " << +packet->type
              << " occupancy: " << RQ.occupancy();
  })

  bool check_thread_id = NAME.find("PTW") != string::npos || (KNOB_VICTIMA && cache_is[IS_L2] && packet->vflag[VF::victima]);

  // check for duplicates in the write queue
  champsim::delay_queue<PACKET>::iterator found_wq = std::find_if(WQ.begin(), WQ.end(), eq_addr<PACKET>(packet->address, match_offset_bits ? 0 : use_offset(packet->type), packet->thread_id, is_tlb || check_thread_id) );

  if (found_wq != WQ.end()) {

    DP(if (warmup_complete[packet->cpu]) std::cout << " MERGED" << std::endl;)

    WQ_MERGED++;

    cacheDataModel->wr_queue[Basic::MERGED]++;
    return 0; // merged index
  }

  // Check for room in the queue
  if (WQ.full()) {
    DP(if (warmup_complete[packet->cpu]) std::cout << " FULL" << std::endl;)

    ++WQ_FULL;

    cacheDataModel->wr_queue[Basic::REJECTED]++;
    return -2;
  }

  // if there is no duplicate, add it to the write queue
  if (warmup_complete[cpu])
    WQ.push_back(*packet);
  else
    WQ.push_back_ready(*packet);

  DP(if (warmup_complete[packet->cpu]) std::cout << " ADDED" << std::endl;)

  WQ_TO_CACHE++;

  cacheDataModel->wr_queue[Basic::ADDED]++;
  // cacheDataModel->wr_queue[Basic::ACCESS]++;
  return WQ.occupancy();
}

int CACHE::prefetch_line(uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata)
{
  pf_requested++;

  PACKET pf_packet;
  pf_packet.type = PREFETCH;
  pf_packet.fill_level = (fill_this_level ? fill_level : lower_level->fill_level);
  pf_packet.pf_origin_level = fill_level;
  pf_packet.pf_metadata = prefetch_metadata;
  pf_packet.cpu = cpu;
  pf_packet.address = pf_addr;
  pf_packet.v_address = virtual_prefetch ? pf_addr : 0;

  //usercode
  pf_packet.ttp = prefetch_metadata;

  if (virtual_prefetch) {
    if (!VAPQ.full()) {
      VAPQ.push_back(pf_packet);
      return 1;
    }
  } else {
    int result = add_pq(&pf_packet);
    if (result != -2) {
      if (result > 0)
        pf_issued++;
      return 1;
    }
  }

  return 0;
}

int CACHE::prefetch_line(uint64_t ip, uint64_t base_addr, uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata)
{
  static bool deprecate_printed = false;
  if (!deprecate_printed) {
    std::cout << "WARNING: The extended signature CACHE::prefetch_line(ip, "
                 "base_addr, pf_addr, fill_this_level, prefetch_metadata) is "
                 "deprecated."
              << std::endl;
    std::cout << "WARNING: Use CACHE::prefetch_line(pf_addr, fill_this_level, "
                 "prefetch_metadata) instead."
              << std::endl;
    deprecate_printed = true;
  }
  return prefetch_line(pf_addr, fill_this_level, prefetch_metadata);
}

void CACHE::va_translate_prefetches()
{
  // TEMPORARY SOLUTION: mark prefetches as translated after a fixed latency
  if (VAPQ.has_ready()) {
    VAPQ.front().address = vmem.va_to_pa(cpu, VAPQ.front().v_address).first;

    // move the translated prefetch over to the regular PQ
    int result = add_pq(&VAPQ.front());

    // remove the prefetch from the VAPQ
    if (result != -2)
      VAPQ.pop_front();

    if (result > 0)
      pf_issued++;
  }
}

int CACHE::add_pq(PACKET* packet)
{
  packet->thread_id = SHARED;
  cacheDataModel->pf_queue[Basic::REQUESTED]++;

  if(packet->address == 0)
  {
    dassert.log("add_pq Address Zero Packet", "instr", packet->instr_id, "addr", intToHex(packet->address), "v_addr", intToHex(packet->v_address), "type", packet->type, "NAME", NAME, "victima", packet->vflag[VF::victima], "pom", packet->pomflag[POM::POM], "\n");
    exit(-1);
  }

  PQ_ACCESS++;

  DP(if (warmup_complete[packet->cpu]) {
    std::cout << "[" << NAME << "_WQ] " << __func__ << " instr_id: " << packet->instr_id << " address: " << std::hex << (packet->address >> use_offset(packet->type));
    std::cout << " full_addr: " << packet->address << " v_address: " << packet->v_address << std::dec << " type: " << +packet->type
              << " occupancy: " << RQ.occupancy();
  })

  bool check_thread_id = NAME.find("PTW") != string::npos || (KNOB_VICTIMA && cache_is[IS_L2] && packet->vflag[VF::victima]);

  // check for the latest wirtebacks in the write queue
  champsim::delay_queue<PACKET>::iterator found_wq = std::find_if(WQ.begin(), WQ.end(), eq_addr<PACKET>(packet->address, match_offset_bits ? 0 : use_offset(packet->type), packet->thread_id, is_tlb||check_thread_id) );

  if (found_wq != WQ.end()) {

    DP(if (warmup_complete[packet->cpu]) std::cout << " MERGED_WQ" << std::endl;)

    packet->data = found_wq->data;
    for (auto ret : packet->to_return)
      ret->return_data(packet);

    WQ_FORWARD++;

    cacheDataModel->pf_queue[Basic::WQ_FWD]++;
    return -1;
  }

  // check for duplicates in the PQ
  auto found = std::find_if(PQ.begin(), PQ.end(), eq_addr<PACKET>(packet->address, use_offset(packet->type), packet->thread_id, is_tlb));
  if (found != PQ.end()) {
    DP(if (warmup_complete[packet->cpu]) std::cout << " MERGED_PQ" << std::endl;)

    found->fill_level = std::min(found->fill_level, packet->fill_level);
    packet_dep_merge(found->to_return, packet->to_return);

    PQ_MERGED++;

    cacheDataModel->pf_queue[Basic::MERGED]++;
    return 0;
  }

  // check occupancy
  if (PQ.full()) {

    DP(if (warmup_complete[packet->cpu]) std::cout << " FULL" << std::endl;)

    PQ_FULL++;

    cacheDataModel->pf_queue[Basic::REJECTED]++;
    return -2; // cannot handle this request
  }

  // track cycle stamp
  packet->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_QUEUE] = current_cycle;

  // if there is no duplicate, add it to PQ
  if (warmup_complete[cpu])
    PQ.push_back(*packet);
  else
    PQ.push_back_ready(*packet);

  DP(if (warmup_complete[packet->cpu]) std::cout << " ADDED" << std::endl;)

  PQ_TO_CACHE++;

  cacheDataModel->pf_queue[Basic::ADDED]++;
  // cacheDataModel->pf_queue[Basic::ACCESS]++;
  return PQ.occupancy();
}

void CACHE::return_data(PACKET* packet)
{
  PACKET handle_pkt = *packet;

  // check MSHR information
  bool check_thread_id = NAME.find("PTW") != string::npos || (KNOB_VICTIMA && cache_is[IS_L2] && packet->vflag[VF::victima]);

  auto mshr_entry = std::find_if(MSHR.begin(), MSHR.end(), eq_addr<PACKET>(packet->address, use_offset(packet->type), packet->thread_id, is_tlb || check_thread_id) );
  auto first_unreturned = std::find_if(MSHR.begin(), MSHR.end(), [](auto x) { return x.event_cycle == std::numeric_limits<uint64_t>::max(); });

  DataType foundDtype = DataType::INVALID;
  if(handle_pkt.type != TRANSLATION)
  {
    foundDtype = DataType::DATA;
  }
  else
  {
    int dist = current_cycle - mshr_entry->type_cycle_enqueued[CYCLE_ENQ::TS_ADD_MSHR];
    if(dist < 7 && cache_is[IS_STLB])
    {
      cout << "stlb, " << dist << ", " << packet->hit_where << '\n';
    }
    if(handle_pkt.translation_level == 1) foundDtype = DataType::PTE;      
    else if(handle_pkt.translation_level == 2) foundDtype = DataType::PMD;      
    else if(handle_pkt.translation_level == 3) foundDtype = DataType::PUD;      
    else if(handle_pkt.translation_level == 4) foundDtype = DataType::PGD;      
  }

  // assign data type for this block
  mshr_entry->dtype = foundDtype;

  if(KNOB_VICTIMA && cache_is[IS_STLB] && KNOB_IDEAL_VICTIMA == 0)
  {
    // count:
    // We sent out actual and a dummy packet.
    // Data can be brought by dummy (DP) and actual (AP) packet.
    // We release when we receive actual packet.
    // Possibility of packets sent:
    // Case 1: AP/L2 and DP/PTW.
    // Case 2: AP/PTW and DP/L2

    // Order of receiving:
    // Case 1: AP/L2 DP/PTW --> (A) AP/L2 missed wait for DP/PTW (B) Otherwise release packet
    // Case 2: AP/PTW DP/L2 --> Release Packet
    // Case 3: DP/L2 AP/PTW --> Wait for AP
    // Case 4: DP/PTW AP/L2 --> AP/L2 missed used value from DP/PTW 

    if(mshr_entry == MSHR.end())
    {
      return;
    }

    bool release = false;

    if(mshr_entry->mshr_state == VF::MSHR_WAIT_DP && packet->vflag[VF::ptw_copy]) // to resolve case 1
    {
      release = true;
    }
    else if(mshr_entry->mshr_state == VF::MSHR_WAIT_AP && packet->vflag[VF::victima]) // to resolve case 4
    {
      release = true;
    }
    else if(mshr_entry->vflag[VF::recv_victima])
    {
      return;
    }
    else if(packet->vflag[VF::PACKET_AP_RECV])
    {
      //case1
      if(packet->vflag[VF::victima])// victima L2
      {
        // miss at L2
        if(packet->vflag[VF::victima_acutal_packet_miss])
        {
          mshr_entry->mshr_state = VF::MSHR_WAIT_DP;
          return;
        }
        // all good: release
        else
        {
          release = true;
        }
      }
      //case2; else sent by PTW
      else
      {
        release = true;
      }
    }
    else if(packet->vflag[VF::PACKET_DP_RECV])
    {
      //case3
      if(packet->vflag[VF::victima])
      {
        return;
      }
      else //case4
      {
        mshr_entry->data = packet->data;
        mshr_entry->mshr_state = VF::MSHR_WAIT_AP;
        return;
      }
    }

    if(release)
    {
      mshr_entry->vflag[VF::recv_victima] = true;

      // MSHR holds the most updated information about this request
      mshr_entry->data = packet->data;
      mshr_entry->pf_metadata = packet->pf_metadata;
      mshr_entry->event_cycle = current_cycle + (warmup_complete[cpu] ? FILL_LATENCY : 0);
      mshr_entry->hit_where = packet->hit_where;
      mshr_entry->page_fault = packet->page_fault;
    }
  }
  else
  {

    // sanity check
    if (mshr_entry == MSHR.end()) {

      std::cerr << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << packet->instr_id << " cannot find a matching entry!";
      std::cerr << " address: " << std::hex << packet->address;
      std::cerr << " v_address: " << packet->v_address;
      std::cerr << " address: " << (packet->address >> use_offset(packet->type)) << std::dec;
      std::cerr << " event: " << packet->event_cycle << " current: " << current_cycle << std::endl;
      assert(0);
    }

    // MSHR holds the most updated information about this request
    mshr_entry->data = packet->data;
    mshr_entry->pf_metadata = packet->pf_metadata;
    mshr_entry->event_cycle = current_cycle + (warmup_complete[cpu] ? FILL_LATENCY : 0);
    mshr_entry->hit_where = packet->hit_where;
    mshr_entry->page_fault = packet->page_fault;
    mshr_entry->pomflag[POM::POM_MISS] = packet->pomflag[POM::POM_MISS];

    // PTW has set POM_TO_PTW_FINI to 1, get this value as handle_fill needs it to distinguish
    mshr_entry->pomflag[POM::POM_TO_PTW_FINI] = packet->pomflag[POM::POM_TO_PTW_FINI];

  }
  
  DP(if (warmup_complete[packet->cpu]) {
    std::cout << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << mshr_entry->instr_id;
    std::cout << " address: " << std::hex << (mshr_entry->address >> use_offset(packet->type)) << " full_addr: " << mshr_entry->address;
    std::cout << " data: " << mshr_entry->data << std::dec;
    std::cout << " index: " << std::distance(MSHR.begin(), mshr_entry) << " occupancy: " << get_occupancy(0, 0);
    std::cout << " event: " << mshr_entry->event_cycle << " current: " << current_cycle << std::endl;
  });

  // Order this entry after previously-returned entries, but before non-returned
  // entries
  std::iter_swap(mshr_entry, first_unreturned);

  // track hit where for each at these structure
  cacheDataModel->readmiss_hitwhere[mshr_entry->hit_where]++;
}

uint32_t CACHE::get_occupancy(uint8_t queue_type, uint64_t address)
{
  if (queue_type == 0)
    return std::count_if(MSHR.begin(), MSHR.end(), is_valid<PACKET>());
  else if (queue_type == 1)
    return RQ.occupancy();
  else if (queue_type == 2)
    return WQ.occupancy();
  else if (queue_type == 3)
    return PQ.occupancy();

  return 0;
}

uint32_t CACHE::get_size(uint8_t queue_type, uint64_t address)
{
  if (queue_type == 0)
    return MSHR_SIZE;
  else if (queue_type == 1)
    return RQ.size();
  else if (queue_type == 2)
    return WQ.size();
  else if (queue_type == 3)
    return PQ.size();

  return 0;
}

bool CACHE::should_activate_prefetcher(int type) { return (1 << static_cast<int>(type)) & pref_activate_mask; }

void CACHE::print_deadlock()
{
  if (!std::empty(MSHR)) {
    std::cout << NAME << " MSHR Entry" << std::endl;
    std::size_t j = 0;
    for (PACKET entry : MSHR) {
      std::cout << "[" << NAME << " MSHR] entry: " << j++ << " instr_id: " << entry.instr_id;
      std::cout << " address: " << std::hex << (entry.address >> LOG2_BLOCK_SIZE) << " full_addr: " << entry.address << std::dec << " type: " << +entry.type;
      std::cout << " fill_level: " << +entry.fill_level << " event_cycle: " << entry.event_cycle << std::endl;
      cout << "hw, " << entry.hit_where <<", " << entry.data << '\n';
      cout << "recv, " << entry.vflag[VF::recv_victima] << ", mshr_state, " << entry.mshr_state << ", AP, " << entry.vflag[VF::PACKET_AP_RECV] << ", DP, " << entry.vflag[VF::PACKET_DP_RECV] << ", ptw_copy, " << entry.vflag[VF::ptw_copy] << ", victima, " << entry.vflag[VF::victima] << ", victima_miss, " << entry.vflag[VF::victima_acutal_packet_miss] << ", dummy, " << entry.vflag[VF::victima_dumy] << ", th, " << entry.thread_id << '\n';
    }
  } else {
    std::cout << NAME << " MSHR empty" << std::endl;
  }

  if(!empty(RQ))
  {
    cout << NAME << " RQ " << '\n';
    for(auto entry: RQ)
      cout <<"RQ, "<< NAME << ", addr, " <<std::hex<<entry.address<<std::dec<<", ins, "<<entry.instr_id<<", type, "<<(int)entry.type<<", th, "<<entry.thread_id<<", victima, " << entry.vflag[VF::victima] << ", dummy, " << entry.vflag[VF::PACKET_DP_RECV] << ", ptwcopy, " << entry.vflag[VF::ptw_copy] << '\n'; 
  }

  if(!empty(WQ))
  {
    cout << NAME << " WQ " << '\n';
    for(auto entry: WQ)
      cout <<"WQ, "<< NAME << ", addr, " <<std::hex<<entry.address<<std::dec<<", ins, "<<entry.instr_id<<", type, "<<(int)entry.type<<", th, "<<entry.thread_id<<", victima, " << entry.vflag[VF::victima] << ", dummy, " << entry.vflag[VF::PACKET_DP_RECV] << ", ptwcopy, " << entry.vflag[VF::ptw_copy] << '\n'; 
  }
}

// peek L2 with virtual address from STLB miss
pair<bool, PTEHolder> CACHE::victima_peek_singleline(const PACKET handle_pkt)
{
  pair<bool, PTEHolder> ret;
  uint32_t set = get_set(handle_pkt.type, handle_pkt.v_address, 1);
  uint32_t way = get_way(handle_pkt.type, handle_pkt.v_address, set, handle_pkt.thread_id, 1);
  
  BLOCK* hit_block = &block[set * NUM_WAY + way];
  // extra check to test if block is victima block or not
  bool hit = way < NUM_WAY && hit_block->victima_block;

  // if hit, we now need exact PTE we are looking for, We have pa of earlier PTE that brought this cache block at L2. We will
  // use that PA to for cache block and use offset from VA
  if(hit)
  {
    int cpuid = KNOB_SMT_ENABLE * handle_pkt.cpu + handle_pkt.thread_id;
    // we have phy address of PTE in pt which brought this cache block (needs to zero out last 6-bit to get cacheblock address)
    // we have virt_addr that points to one of the PTE present in cache block
    // base_page_level_1_pt + VPN[8:3] --> cache_block
    // hence, cache_block + VPN[2:0] --> PTE address
    uint64_t block_offset = (handle_pkt.v_address >> (LOG2_PAGE_SIZE)) & 0x7;
    block_offset = block_offset << 3;
    // cache block address from phy address, zeros out last 6bit
    uint64_t cache_block_addr = (hit_block->original_pagetable_cacheblock_address >> 6);
    // return 6 bits as zeroed
    cache_block_addr = cache_block_addr << 6;
    // append block_offset of 6bits
    uint64_t new_addr = cache_block_addr | block_offset;
    
    ret = process_page_table->get_pte(cpuid, new_addr, hit_block->translation_level_if_pagetable_block);
  }
  return ret;
}

// track accessed page and its blocks for tracking capacity misses
void CACHE::func_track_workingset(uint64_t addr)
{
  uint64_t page = addr & ~(PAGE_SIZE-1);
  
  auto page_it = page_to_block.find(page);
  if(page_it == page_to_block.end())
    page_to_block[page] = bitset<64>(0);
  
  if(!is_tlb)
  {
    uint64_t cache_block_index = (addr > 6) & 0x3f;
    page_to_block[page].set(cache_block_index, 1);
  }
}

// tracking data access latency: miss  
void CACHE::func_track_missfulfill_access_latency(uint64_t eq_cycle)
{
  int diff = current_cycle - eq_cycle + 1;
  cacheDataModel->miss_fulfilled_latency->add_data_freq(diff, 1);
}

// tracking data access latency: miss  
void CACHE::func_track_miss_access_latency(uint64_t eq_cycle)
{
  int diff = current_cycle - eq_cycle + 1;
  cacheDataModel->miss_access_latency->add_data_freq(diff, 1);
}

// tracking data access latency: hit 
void CACHE::func_track_hit_access_latency(uint64_t eq_cycle, int metadata)
{
  int diff = current_cycle - eq_cycle + 1;
  cacheDataModel->hit_access_latency->add_data_freq(diff, 1);

  // track victima packet access latency
  if(metadata)
  {
    cacheDataModel->victima_access_latency_at_l2->add_data_freq(diff, 1);
  }
}


void CACHE::func_track_evicted_pte(uint64_t v_address, uint64_t data)
{
    // Only process when we have enough history
    if (eviction_history_pte.size() >= LIMIT_HITORY_LEN_EVICTED_PTE)
    {
        constexpr size_t K = 8; // we care about distances [0..7]
        std::vector<uint64_t> vpages; vpages.reserve(eviction_history_pte.size());
        std::vector<uint64_t> ppages; ppages.reserve(eviction_history_pte.size());

        // 1) Build unique-by-virtual-page lists (dedupe)
        //    If multiple entries share the same vpage, we keep the first physical seen.
        std::unordered_set<uint64_t> seen_vpages;
        vpages.clear(); ppages.clear();
        vpages.reserve(eviction_history_pte.size());
        ppages.reserve(eviction_history_pte.size());

        for (const auto &kv : eviction_history_pte) {
            uint64_t vpage = kv.first  >> LOG2_PAGE_SIZE;
            if (seen_vpages.insert(vpage).second) {
                vpages.push_back(vpage);
                ppages.push_back(kv.second >> LOG2_PAGE_SIZE);
            }
        }

        // 2) Track offset variation (lowest 3 bits of the vpage index)
        std::vector<uint64_t> seen_offset(K, 0);
        for (uint64_t vp : vpages) {
            size_t off = static_cast<size_t>(vp & (K - 1)); // vp % 8
            seen_offset[off]++;
        }

        // 3) Pairwise distance histograms (virtual and physical)
        std::vector<uint64_t> vpages_cluster(K, 0);
        std::vector<uint64_t> ppages_cluster(K, 0);

        // Count unordered pairs once (i < j)
        for (size_t i = 0; i < vpages.size(); ++i) {
            for (size_t j = i + 1; j < vpages.size(); ++j) {
                uint64_t vdist = (vpages[i] > vpages[j]) ? (vpages[i] - vpages[j]) : (vpages[j] - vpages[i]);
                if (vdist < K) vpages_cluster[vdist]++;

                uint64_t pdist = (ppages[i] > ppages[j]) ? (ppages[i] - ppages[j]) : (ppages[j] - ppages[i]);
                if (pdist < K) ppages_cluster[pdist]++;
            }
        }

        // 4) Write into your hitmaps (cap the second dimension as you intended)
        // NOTE: Ensure transition_hitmap_* second dimension is large enough.
        for (size_t i = 0; i < K; ++i) {
            // offset map: bucket by count, capped at K (or whatever max you support)
            size_t off_bucket = static_cast<size_t>(seen_offset[i] > K ? K : seen_offset[i]);
            transition_hitmap_for_offset[i][off_bucket]++;

            // virtual page cluster: bucket by count, capped
            size_t vp_bucket  = static_cast<size_t>(vpages_cluster[i] > K ? K : vpages_cluster[i]);
            transition_hitmap_for_vp_page[i][vp_bucket]++;

            // physical page cluster (if you maintain it similarly)
            size_t pp_bucket  = static_cast<size_t>(ppages_cluster[i] > K ? K : ppages_cluster[i]);
            transition_hitmap_for_pp_page[i][pp_bucket]++;
        }

        // Reset history window
        eviction_history_pte.clear();
    }

    // Append newest eviction
    // eviction_history_pte is assumed to be a map<va, pa> (or unordered_map)
    eviction_history_pte.insert({v_address, data});
}

// check if we are above limit
// remove entry is hash_cache reached to its limit
// invalidation packet for same entry sent to L2
void CACHE::adjust_hashcache()
{
  //*** test: L2 rq occupancy alredy done before ***//
  // invalidate corresponding entry is in the L2, since we are removing hash_entry
  pair<string, uint64_t> hash_entry = hash_cache.front();

  // prepare packet to invalidate entry in L2 if it exists
  PACKET invpacket;
  invpacket.address = hash_entry.second;
  invpacket.vflag[VF::INVALIDATE_PACKET] = 1;

  // packet sent to L2
  l2cache->add_rq(&invpacket);

  // remove entry from hash_cache
  hash_cache.pop_front();
}

// Proposed IDEA @STLB
// true: write as ext-victima packet
int CACHE::use_cluster(PACKET* packet)
{
  // test if cluster-8 available to be able to written in L2
  pair<bool, PTEContainer> ret = collect_pte[packet->thread_id].test();
  if(ret.first)
  {
    // generate hash for cluster-8
    string hash_str= "";
    for(auto entry: ret.second.collection)
      hash_str += to_string(entry.vaddr) + "_";
    hash_cache.push_back(make_pair(hash_str, ret.second.collection.front().vaddr));

    // store this buffer of 8 PTE in L2 cache block using current packet
    packet->vflag[VF::EXT_VICTIMA_PACKET] = 1;
    packet->pte_container = ret.second;
    return true;
  }
  return false;
}

// Proposed IDEA @STLB
// -1: cannot be inserted. write as victima packet
// -2: insert done. write later collect now
int CACHE::add_to_cluster(PACKET* packet)
{
  uint64_t offset = (packet->address >> LOG2_PAGE_SIZE) & 0x7;
  uint64_t vp = (packet->address & ~(PAGE_SIZE-1));
  uint64_t pp = (packet->data & ~(PAGE_SIZE-1));

  // test occupancy in PTEContainer specific to this offset
  if(collect_pte[packet->thread_id].isSpaceAvail(offset))
  {
    // insert in PTEContainer --> insertion done
    collect_pte[packet->thread_id].insert(offset, PTEclass(vp, pp, packet->thread_id));

    // since space is avail, we dont need to write it yet
    return -2;
  }
  return -1;
}

void CACHE::func_return(PACKET* packet)
{
  for(auto ret: packet->to_return)
  {
    ret->return_data(packet);
  }
}

CacheBlock* CACHE::func_test_page_table(BLOCK& fill_block)
{
  
  return nullptr;
}