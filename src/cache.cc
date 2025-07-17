#include "cache.h"

#include <algorithm>
#include <iterator>

#include "champsim.h"
#include "champsim_constants.h"
#include "util.h"
#include "vmem.h"
#include "user.h"

#ifndef SANITY_CHECK
#define NDEBUG
#endif

#define SHARED 3

// Extra configguration
extern int KNOB_TRANSLATION_QUEUE;
extern int KNOB_STLB_DO_NOT_TRACK_MISS;
extern int KNOB_VICTIMA;

// illusiong of stored cache line by 8byte granularity
extern map<uint32_t, uint32_t> l2_pte_map;

extern VirtualMemory vmem;
extern uint8_t warmup_complete[NUM_CPUS];

void CACHE::handle_fill()
{
  while (writes_available_this_cycle > 0) {
    auto fill_mshr = MSHR.begin();
    if (fill_mshr == std::end(MSHR) || fill_mshr->event_cycle > current_cycle)
    {
      cacheDataModel->mshr_queue_stalls[Stall::OP_PENALTY]++;
      return;
    }
    
    // find victim
    uint32_t set = get_set(fill_mshr->address);

    auto set_begin = std::next(std::begin(block), set * NUM_WAY);
    auto set_end = std::next(set_begin, NUM_WAY);
    auto first_inv = std::find_if_not(set_begin, set_end, is_valid<BLOCK>());
    uint32_t way = std::distance(set_begin, first_inv);
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
    
    MSHR.erase(fill_mshr);
    writes_available_this_cycle--;
    
    cacheDataModel->mshr_queue[Basic::ACCESS]++;
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

    bool write_true = false;

    // handle the oldest entry
    PACKET& handle_pkt = WQ.front();

    // access cache
    uint32_t set = get_set(handle_pkt.address, handle_pkt.vflag[VF::victima]);
    uint32_t way = get_way(handle_pkt.address, set, handle_pkt.thread_id, handle_pkt.vflag[VF::victima]);
    uint32_t off = get_offset(handle_pkt.address);
    
    BLOCK& fill_block = block[set * NUM_WAY + way];
    bool hit = way < NUM_WAY;

    uint64_t vp = (handle_pkt.address & ~(PAGE_SIZE-1));
    uint64_t pp = (handle_pkt.data & ~(PAGE_SIZE-1));

    bool test = l2_pte_map.find(vp)!=l2_pte_map.end();
    if(handle_pkt.vflag[VF::victima] && KNOB_VICTIMA && cache_is[IS_L2])
    {
      hit = hit && test;
      if(hit)
      {
        BLOCK* hit_block = &block[set * NUM_WAY + way];
        hit = (hit && hit_block->victima_block) && (handle_pkt.thread_id == hit_block->thread_id || hit_block->thread_id == SHARED);
      }
    }

    if (hit) // HIT
    {
      impl_replacement_update_state(handle_pkt.cpu, set, way, fill_block.address, handle_pkt.ip, 0, handle_pkt.type, 1);

      write_true = true;

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
        write_true = true;
      }
      
      if (!success)
      {
        cacheDataModel->wr_queue_stalls[Stall::OP_FAIL_PENALTY]++;
        return;
      }

      cacheDataModel->wr_queue[Basic::MISS]++;
    }

    if(write_true)
    {
      uint32_t offset = handle_pkt.address >> LOG2_PAGE_SIZE & 0x7;
      fill_block.updateUsage(offset);

      // writing stlb PTE to L2
      if(KNOB_VICTIMA && cache_is[IS_L2] && handle_pkt.vflag[VF::victima])
      {
        auto find_page = l2_pte_map.find(vp);
        if(find_page == l2_pte_map.end())
        {
          l2_pte_map.insert({vp, pp});
        }
      }
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
    assert(handle_pkt.thread_id!=-1);

    // A (hopefully temporary) hack to know whether to send the evicted paddr or
    // vaddr to the prefetcher
    ever_seen_data |= (handle_pkt.v_address != handle_pkt.ip);

    uint32_t set = get_set(handle_pkt.address, handle_pkt.vflag[VF::victima]);
    uint32_t way = get_way(handle_pkt.address, set, handle_pkt.thread_id, handle_pkt.vflag[VF::victima]);
    uint32_t off = get_offset(handle_pkt.address);

    bool hit = way < NUM_WAY;

    uint64_t vp = (handle_pkt.address & ~(PAGE_SIZE-1));
    uint64_t pp = (handle_pkt.data & ~(PAGE_SIZE-1));

    bool test = l2_pte_map.find(vp)!=l2_pte_map.end();
    if(handle_pkt.vflag[VF::victima] && KNOB_VICTIMA && cache_is[IS_L2])
    {
      hit = hit && test;
      if(hit)
      {
        BLOCK* hit_block = &block[set * NUM_WAY + way];
        // cout << "hit," << hit << ", vic_block," << (hit_block->victima_block) << ", pkt_thread," << (handle_pkt.thread_id) << ", block_thread," << (hit_block->thread_id) << ", " << (hit_block->came_from_request) << '\n';
        hit = (hit && hit_block->victima_block) && (handle_pkt.thread_id == hit_block->thread_id || hit_block->thread_id == SHARED);
      }
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

    uint32_t set = get_set(handle_pkt.address, handle_pkt.vflag[VF::victima]);
    uint32_t way = get_way(handle_pkt.address, set, handle_pkt.thread_id, handle_pkt.vflag[VF::victima]);

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

  cout << "readhit: " << NAME <<", " << current_cycle << ", th, " << handle_pkt.thread_id << ", addr, " <<std::hex<<handle_pkt.address<<std::dec<<", type, " << (int)handle_pkt.type << ", ptw, " << handle_pkt.vflag[VF::ptw_copy] << ", dummy, " << handle_pkt.vflag[VF::PACKET_DP_RECV] << ", ins, " << handle_pkt.instr_id << ", V, " << handle_pkt.vflag[VF::victima] <<", " << handle_pkt.vflag[VF::victima_acutal_packet_miss] <<", H, " << handle_pkt.hit_where << '\n';

  DP(if (warmup_complete[handle_pkt.cpu]) {
    std::cout << "[" << NAME << "] " << __func__ << " hit";
    std::cout << " instr_id: " << handle_pkt.instr_id << " address: " << std::hex << (handle_pkt.address >> OFFSET_BITS);
    std::cout << " full_addr: " << handle_pkt.address;
    std::cout << " full_v_addr: " << handle_pkt.v_address << std::dec;
    std::cout << " type: " << +handle_pkt.type;
    std::cout << " cycle: " << current_cycle << std::endl;
  });

  BLOCK& hit_block = block[set * NUM_WAY + way];

  handle_pkt.data = hit_block.data;
  handle_pkt.hit_where = cache_id;

  if(KNOB_VICTIMA && cache_is[CACHE_ID::IS_L2] && handle_pkt.vflag[VF::victima])
  {
    uint64_t vp_addr = handle_pkt.address & ~(PAGE_SIZE-1);
    auto found = l2_pte_map.find(vp_addr);
    if(found != l2_pte_map.end())
      handle_pkt.data = found->second;
  }

  // update prefetcher on load instruction
  if (should_activate_prefetcher(handle_pkt.type) && handle_pkt.pf_origin_level < fill_level) {
    cpu = handle_pkt.cpu;
    uint64_t pf_base_addr = (virtual_prefetch ? handle_pkt.v_address : handle_pkt.address) & ~bitmask(match_offset_bits ? 0 : OFFSET_BITS);
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
}

bool CACHE::readlike_miss(PACKET& handle_pkt)
{
  cout << "readmiss: "<< NAME <<", " << current_cycle << ", th, " << handle_pkt.thread_id << ", addr, " <<std::hex<<handle_pkt.address<<std::dec<<", type, " << (int)handle_pkt.type << ", ptw, " << handle_pkt.vflag[VF::ptw_copy] << ", dummy, " << handle_pkt.vflag[VF::PACKET_DP_RECV] << ", ins, " << handle_pkt.instr_id << ", V, " << handle_pkt.vflag[VF::victima] <<", " << handle_pkt.vflag[VF::victima_acutal_packet_miss] <<", H, " << handle_pkt.hit_where << '\n';

  if(KNOB_VICTIMA)
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
    std::cout << " instr_id: " << handle_pkt.instr_id << " address: " << std::hex << (handle_pkt.address >> OFFSET_BITS);
    std::cout << " full_addr: " << handle_pkt.address;
    std::cout << " full_v_addr: " << handle_pkt.v_address << std::dec;
    std::cout << " type: " << +handle_pkt.type;
    std::cout << " cycle: " << current_cycle << std::endl;
  });

  // check mshr
  bool check_thread_id = NAME.find("PTW") != string::npos || (KNOB_VICTIMA && cache_is[IS_L2] && handle_pkt.vflag[VF::victima]);
  auto mshr_entry = std::find_if(MSHR.begin(), MSHR.end(), eq_addr<PACKET>(handle_pkt.address, OFFSET_BITS, handle_pkt.thread_id, is_tlb||check_thread_id));
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
    if (mshr_full)  // not enough MSHR resource
    {
      cacheDataModel->adv_stats[AdvStat::CASCADE_STALL_READLIKEMISS_MSHR_FULL]++;
      cacheDataModel->mshr_queue[Basic::REJECTED]++;
      return false; // TODO should we allow prefetches anyway if they will not
                    // be filled to this level?
    }
    bool is_read = prefetch_as_load || (handle_pkt.type != PREFETCH);

    // check to make sure the lower level queue has room for this read miss
    int queue_type = (is_read) ? 1 : 3;
    if (lower_level->get_occupancy(queue_type, handle_pkt.address) == lower_level->get_size(queue_type, handle_pkt.address))
    {
      cacheDataModel->adv_stats[AdvStat::CASCADE_STALL_READLIKEMISS_NEXTLEVEL_FULL]++;
      return false;
    }

    PACKET newPacket = handle_pkt;
    if(KNOB_VICTIMA && cache_is[IS_STLB])
    {
      if(l2cache->get_occupancy(1,0) == l2cache->get_size(1,0))
      {
        return false;
      }

      newPacket.address = handle_pkt.address;
      newPacket.v_address = handle_pkt.v_address;
      newPacket.to_return = {this};
      newPacket.vflag[VF::victima] = true;
      newPacket.thread_id = handle_pkt.thread_id;

      // soft lookup
      bool found = ((CACHE*)l2cache->getObject())->peek_singleline(newPacket);

      // to fix difference in returned physical address ex. F: 346681344, S: 4641652728
      // do softlookup, if not in cache then set dumy status to simulate traffic and dont use its results.
      if(found)
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

    // Allocate an MSHR
    if (handle_pkt.fill_level <= fill_level  && !(cache_is[CACHE_ID::IS_STLB] &&  KNOB_STLB_DO_NOT_TRACK_MISS)) {
      auto it = MSHR.insert(std::end(MSHR), handle_pkt);
      it->cycle_enqueued = current_cycle;
      it->event_cycle = std::numeric_limits<uint64_t>::max();

      cacheDataModel->mshr_queue[Basic::ADDED]++;

      // placed here making sure MSHR entry is first inserted. The reason is it might receive hit in WQ of L2, that time it will
      // try to return data to STLB and wont find MSHR hence to prevent such situtation
      if(KNOB_VICTIMA && cache_is[IS_STLB])
      {
        int status = l2cache->add_rq(&newPacket);
      }

      cout << "rec, " << current_cycle << ", addr, "<<std::hex<<newPacket.address<<std::dec<<", ins, " << newPacket.instr_id << '\n';
    }

    if( !(cache_is[CACHE_ID::IS_STLB] &&  KNOB_STLB_DO_NOT_TRACK_MISS))
    {
      if (handle_pkt.fill_level <= fill_level)
        handle_pkt.to_return = {this};
      else
        handle_pkt.to_return.clear();
    }

    if(97655 == handle_pkt.instr_id)
    {
      for(auto ret: handle_pkt.to_return)
        cout <<  ", ins, " << handle_pkt.instr_id <<", "<<NAME << "-->" << ((CACHE*)ret->getObject())->NAME << '\n';
    }
    

    if (!is_read)
      lower_level->add_pq(&handle_pkt);
    else
    {
      lower_level->add_rq(&handle_pkt);
    }
      
  }

  // update prefetcher on load instructions and prefetches from upper levels
  if (should_activate_prefetcher(handle_pkt.type) && handle_pkt.pf_origin_level < fill_level) {
    cpu = handle_pkt.cpu;
    uint64_t pf_base_addr = (virtual_prefetch ? handle_pkt.v_address : handle_pkt.address) & ~bitmask(match_offset_bits ? 0 : OFFSET_BITS);
    handle_pkt.pf_metadata = impl_prefetcher_cache_operate(pf_base_addr, handle_pkt.ip, 0, handle_pkt.type, handle_pkt.pf_metadata);
  }
    
  return true;
}

bool CACHE::filllike_miss(std::size_t set, std::size_t way, PACKET& handle_pkt)
{
  DP(if (warmup_complete[handle_pkt.cpu]) {
    std::cout << "[" << NAME << "] " << __func__ << " miss";
    std::cout << " instr_id: " << handle_pkt.instr_id << " address: " << std::hex << (handle_pkt.address >> OFFSET_BITS);
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

  BLOCK& fill_block = block[set * NUM_WAY + way];
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

      auto result = lower_level->add_wq(&writeback_packet);
      if (result == -2)
      {
        cacheDataModel->adv_stats[AdvStat::CASCADE_STALL_FILLLIKEMISS_NEXTLEVEL_FULL]++;
        return false;
      }

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
    else // clean 
    {
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

    // check for compulsory miss
    if(!fill_block.valid)
    {  
      cacheDataModel->category_of_misses[MISS::COM]++;
    }
    // check for conflict misses && capacity misses
    else
    {
      if(KNOB_VICTIMA)
      {
        if(cache_is[CACHE_ID::IS_STLB] && victima_lookup(handle_pkt.v_address))
        {
          if(l2cache->get_occupancy(2,0) == l2cache->get_size(2,0))
          {
            return false;
          }

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
          l2cache->add_wq(&writeback_packet);
          victima_counters[VC::STLB_EVICT]++;
        }
        else if(cache_is[CACHE_ID::IS_L2] && fill_block.victima_block)
        {
          int usage = fill_block.getUsage();
          victima_block_usage[usage]++;
          victima_counters[VC::L2_EVICT]++;
        }
      }

      // counting the number of times set has seen conflict and as a result a dirty block is sent-back
      // it needs infinit FA cache to keep history
      // cacheDataModel->category_of_misses[MISS::CAP]++;

      // checking for CONFLICT miss only can be tracked.
      {
        auto it = std::find_if(fa_array.begin(), fa_array.end(), eq_addr<BLOCK>(handle_pkt.address, OFFSET_BITS));
        if(it!=fa_array.end())
        {
          cacheDataModel->category_of_misses[MISS::CONF]++;
        }

        // track evicted/overwritten block
        if(fa_array.size() >= FA_SIZE)
          fa_array.pop_back();
        
        auto found_out = find_if(fa_array.begin(), fa_array.end(), eq_addr<BLOCK>(fill_block.address,  match_offset_bits ? 0 : OFFSET_BITS));
        if(found_out==fa_array.end())
        {
          fa_array.push_back(block[set*NUM_WAY + way]);
        }
      }
    }

    if (ever_seen_data)
      evicting_address = fill_block.address & ~bitmask(match_offset_bits ? 0 : OFFSET_BITS);
    else
      evicting_address = fill_block.v_address & ~bitmask(match_offset_bits ? 0 : OFFSET_BITS);

    if (fill_block.prefetch)
      pf_useless++;

    if (handle_pkt.type == PREFETCH)
      pf_fill++;

    fill_block.valid = true;
    fill_block.prefetch = (handle_pkt.type == PREFETCH && handle_pkt.pf_origin_level == fill_level);
    fill_block.dirty = (handle_pkt.type == WRITEBACK || (handle_pkt.type == RFO && handle_pkt.to_return.empty()));
    fill_block.address = handle_pkt.address;
    fill_block.v_address = handle_pkt.v_address;
    fill_block.data = handle_pkt.data;
    fill_block.ip = handle_pkt.ip;
    fill_block.cpu = handle_pkt.cpu;
    fill_block.instr_id = handle_pkt.instr_id;
    fill_block.came_from_request = handle_pkt.type;
    fill_block.m_used = handle_pkt.type==WRITEBACK ? 0: fill_block.m_used;
    fill_block.victima_block = handle_pkt.vflag[VF::victima];
    fill_block.thread_id = handle_pkt.thread_id;
    fill_block.vp_2_pp_map.clear();
  }

  if (warmup_complete[handle_pkt.cpu] && (handle_pkt.cycle_enqueued != 0))
    total_miss_latency += current_cycle - handle_pkt.cycle_enqueued;

  // update prefetcher
  cpu = handle_pkt.cpu;
  handle_pkt.pf_metadata =
      impl_prefetcher_cache_fill((virtual_prefetch ? handle_pkt.v_address : handle_pkt.address) & ~bitmask(match_offset_bits ? 0 : OFFSET_BITS), set, way,
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

uint32_t CACHE::get_set(uint64_t address, bool victima) 
{
  int offset = OFFSET_BITS;
  if(KNOB_VICTIMA && victima && cache_is[IS_L2])
  {
    offset = LOG2_PAGE_SIZE + 3;//3;
  }
  return ((address >> offset) & bitmask(lg2(NUM_SET)));
}

//  |----- TAG/Page Number --------|
//  |------EXTRA------|---PTEO(3b)---|----SET----|---BO(3b)---|
uint32_t CACHE::get_way(uint64_t address, uint32_t set, int th, bool victima)
{
  int offset = OFFSET_BITS;
  if(KNOB_VICTIMA && victima && cache_is[IS_L2])
  {
    // we need page offset hence
    offset = lg2(NUM_SET) + LOG2_PAGE_SIZE + 3;//lg2(NUM_SET) + 3;
  }
  
  auto begin = std::next(block.begin(), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);
  return std::distance(begin, std::find_if(begin, end, eq_addr<BLOCK>(address, offset, th, (is_tlb || (cache_is[IS_L2]&&victima)) )));
}

uint32_t CACHE::get_offset(uint64_t address)
{
  uint32_t offset = address >> LOG2_PAGE_SIZE & 0x7;
  return offset;
}

int CACHE::invalidate_entry(uint64_t inval_addr)
{
  uint32_t set = get_set(inval_addr);
  uint32_t way = get_way(inval_addr, set, -1);

  if (way < NUM_WAY)
    block[set * NUM_WAY + way].valid = 0;

  return way;
}

int CACHE::add_rq(PACKET* packet)
{
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


  cacheDataModel->rd_queue[Basic::REQUESTED]++;

  assert(packet->address != 0);
  RQ_ACCESS++;

  DP(if (warmup_complete[packet->cpu]) {
    std::cout << "[" << NAME << "_RQ] " << __func__ << " instr_id: " << packet->instr_id << " address: " << std::hex << (packet->address >> OFFSET_BITS);
    std::cout << " full_addr: " << packet->address << " v_address: " << packet->v_address << std::dec << " type: " << +packet->type
              << " occupancy: " << RQ.occupancy();
  })

  bool check_thread_id = NAME.find("PTW") != string::npos || (KNOB_VICTIMA && cache_is[IS_L2] && packet->vflag[VF::victima]);

  // check for the latest writebacks in the write queue
  champsim::delay_queue<PACKET>::iterator found_wq = std::find_if(WQ.begin(), WQ.end(), eq_addr<PACKET>(packet->address, match_offset_bits ? 0 : OFFSET_BITS, packet->thread_id, is_tlb || check_thread_id) );
  
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
  auto found_rq = std::find_if(RQ.begin(), RQ.end(), eq_addr<PACKET>(packet->address, OFFSET_BITS, packet->thread_id, is_tlb || check_thread_id) );
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
    std::cout << "[" << NAME << "_WQ] " << __func__ << " instr_id: " << packet->instr_id << " address: " << std::hex << (packet->address >> OFFSET_BITS);
    std::cout << " full_addr: " << packet->address << " v_address: " << packet->v_address << std::dec << " type: " << +packet->type
              << " occupancy: " << RQ.occupancy();
  })

  bool check_thread_id = NAME.find("PTW") != string::npos || (KNOB_VICTIMA && cache_is[IS_L2] && packet->vflag[VF::victima]);

  // check for duplicates in the write queue
  champsim::delay_queue<PACKET>::iterator found_wq = std::find_if(WQ.begin(), WQ.end(), eq_addr<PACKET>(packet->address, match_offset_bits ? 0 : OFFSET_BITS, packet->thread_id, is_tlb || check_thread_id) );

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
  assert(packet->address != 0);
  PQ_ACCESS++;

  DP(if (warmup_complete[packet->cpu]) {
    std::cout << "[" << NAME << "_WQ] " << __func__ << " instr_id: " << packet->instr_id << " address: " << std::hex << (packet->address >> OFFSET_BITS);
    std::cout << " full_addr: " << packet->address << " v_address: " << packet->v_address << std::dec << " type: " << +packet->type
              << " occupancy: " << RQ.occupancy();
  })

  bool check_thread_id = NAME.find("PTW") != string::npos || (KNOB_VICTIMA && cache_is[IS_L2] && packet->vflag[VF::victima]);

  // check for the latest wirtebacks in the write queue
  champsim::delay_queue<PACKET>::iterator found_wq = std::find_if(WQ.begin(), WQ.end(), eq_addr<PACKET>(packet->address, match_offset_bits ? 0 : OFFSET_BITS, packet->thread_id, is_tlb||check_thread_id) );

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
  auto found = std::find_if(PQ.begin(), PQ.end(), eq_addr<PACKET>(packet->address, OFFSET_BITS, packet->thread_id, is_tlb));
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
  cout << "return: " << NAME <<", " << current_cycle << ", th, " << packet->thread_id << ", addr, " <<std::hex<<packet->address<<std::dec<<", type, " << (int)packet->type << ", ptw, " << packet->vflag[VF::ptw_copy] << ", dummy, " << packet->vflag[VF::PACKET_DP_RECV] << ", ins, " << packet->instr_id << ", V, " << packet->vflag[VF::victima] <<", " << packet->vflag[VF::victima_acutal_packet_miss] <<", H, " << packet->hit_where << '\n';

  // check MSHR information
  bool check_thread_id = NAME.find("PTW") != string::npos || (KNOB_VICTIMA && cache_is[IS_L2] && packet->vflag[VF::victima]);

  auto mshr_entry = std::find_if(MSHR.begin(), MSHR.end(), eq_addr<PACKET>(packet->address, OFFSET_BITS, packet->thread_id, is_tlb || check_thread_id) );
  auto first_unreturned = std::find_if(MSHR.begin(), MSHR.end(), [](auto x) { return x.event_cycle == std::numeric_limits<uint64_t>::max(); });

  if(KNOB_VICTIMA && cache_is[IS_STLB])
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
    }
  }
  else
  {

    // sanity check
    if (mshr_entry == MSHR.end()) {
      std::cerr << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << packet->instr_id << " cannot find a matching entry!";
      std::cerr << " address: " << std::hex << packet->address;
      std::cerr << " v_address: " << packet->v_address;
      std::cerr << " address: " << (packet->address >> OFFSET_BITS) << std::dec;
      std::cerr << " event: " << packet->event_cycle << " current: " << current_cycle << std::endl;
      assert(0);
    }

    // MSHR holds the most updated information about this request
    mshr_entry->data = packet->data;
    mshr_entry->pf_metadata = packet->pf_metadata;
    mshr_entry->event_cycle = current_cycle + (warmup_complete[cpu] ? FILL_LATENCY : 0);
    mshr_entry->hit_where = packet->hit_where;

  }
  
  DP(if (warmup_complete[packet->cpu]) {
    std::cout << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << mshr_entry->instr_id;
    std::cout << " address: " << std::hex << (mshr_entry->address >> OFFSET_BITS) << " full_addr: " << mshr_entry->address;
    std::cout << " data: " << mshr_entry->data << std::dec;
    std::cout << " index: " << std::distance(MSHR.begin(), mshr_entry) << " occupancy: " << get_occupancy(0, 0);
    std::cout << " event: " << mshr_entry->event_cycle << " current: " << current_cycle << std::endl;
  });

  // Order this entry after previously-returned entries, but before non-returned
  // entries
  std::iter_swap(mshr_entry, first_unreturned);
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

bool CACHE::peek_singleline(PACKET handle_pkt)
{
  // found in cache
  uint32_t set = get_set(handle_pkt.address, handle_pkt.vflag[VF::victima]);
  uint32_t way = get_way(handle_pkt.address, set, handle_pkt.thread_id, handle_pkt.vflag[VF::victima]);
  bool hit = way < NUM_WAY;

  uint64_t vp = (handle_pkt.address & ~(PAGE_SIZE-1));
  uint64_t pp = (handle_pkt.data & ~(PAGE_SIZE-1));

  bool test = l2_pte_map.find(vp)!=l2_pte_map.end();
  if(handle_pkt.vflag[VF::victima] && KNOB_VICTIMA && cache_is[IS_L2])
  {
    hit = hit && test;
    if(hit)
    {
      BLOCK* hit_block = &block[set * NUM_WAY + way];
      hit = (hit && hit_block->victima_block) && (handle_pkt.thread_id == hit_block->thread_id || hit_block->thread_id == SHARED);
    }
  }

  return hit;
}
