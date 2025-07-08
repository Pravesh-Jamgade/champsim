#include "ptw.h"

#include "champsim.h"
#include "util.h"
#include "vmem.h"
#include "DataModel.h"

#include "cache.h"

// Extra configguration
extern int KNOB_TTP;
extern int KNOB_SMT_ENABLE;

extern VirtualMemory vmem;
extern uint8_t warmup_complete[NUM_CPUS];

PageTableWalker::PageTableWalker(string v1, uint32_t cpu, unsigned fill_level, uint32_t v2, uint32_t v3, uint32_t v4, uint32_t v5, uint32_t v6, uint32_t v7,
                                 uint32_t v8, uint32_t v9, uint32_t v10, uint32_t v11, uint32_t v12, uint32_t v13, unsigned latency, MemoryRequestConsumer* ll, CACHE* llc)
    : champsim::operable(1), MemoryRequestConsumer(fill_level), MemoryRequestProducer(ll), NAME(v1), cpu(cpu), MSHR_SIZE(v11), MAX_READ(v12),
      MAX_FILL(v13), RQ{v10, latency}, PSCL5{"PSCL5", 4, v2, v3}, // Translation from L5->L4
      PSCL4{"PSCL4", 3, v4, v5},                                  // Translation from L5->L3
      PSCL3{"PSCL3", 2, v6, v7},                                  // Translation from L5->L2
      PSCL2{"PSCL2", 1, v8, v9},                                  // Translation from L5->L1
      llcObject(llc)
{
  ptw_datamodel = new PTWDataModel(cpu);

  // supporting 16 threads
  for(int i=0; i< 16; i++)
    CR3_addr.push_back(vmem.get_pte_pa(i, 0, vmem.pt_levels).first);
}

void PageTableWalker::handle_read()
{
  int reads_this_cycle = MAX_READ;

  while (reads_this_cycle > 0 && RQ.has_ready() && std::size(MSHR) != MSHR_SIZE) {
    PACKET& handle_pkt = RQ.front();

    DP(if (warmup_complete[packet->cpu]) {
      std::cout << "[" << NAME << "] " << __func__ << " instr_id: " << handle_pkt.instr_id;
      std::cout << " address: " << std::hex << (handle_pkt.address >> LOG2_PAGE_SIZE) << " full_addr: " << handle_pkt.address;
      std::cout << " full_v_addr: " << handle_pkt.v_address;
      std::cout << " data: " << handle_pkt.data << std::dec;
      std::cout << " translation_level: " << +handle_pkt.translation_level;
      std::cout << " event: " << handle_pkt.event_cycle << " current: " << current_cycle << std::endl;
    });

    assert(handle_pkt.thread_id!=-1);

    // initalizing ptw from root
    uint8_t ptw_level = vmem.pt_levels - 1;
    // first pa to start page table walk
    uint64_t next_pt_addr = splice_bits(CR3_addr[handle_pkt.thread_id], vmem.get_offset(handle_pkt.address, ptw_level) * PTE_BYTES, LOG2_PAGE_SIZE);
    bool miss_at_root = true;
    if(0)
    {
      // optimized
      for (auto pscl : {&PSCL5, &PSCL4, &PSCL3, &PSCL2}) {
        if (auto check_addr = pscl->check_hit(next_pt_addr, handle_pkt.thread_id); check_addr.has_value()) {
          next_pt_addr = check_addr.value();
          ptw_level = pscl->level - 1; 
        }
      }
    }
    else
    {
      //detailed
      // look for this levels PSC, if corresponding entry found then we can skip the memory access for this level
      for (auto pscl : {&PSCL5, &PSCL4, &PSCL3, &PSCL2}) {
        if(ptw_level != pscl->level)
          continue;
        if (auto check_addr = pscl->check_hit(next_pt_addr, handle_pkt.thread_id); check_addr.has_value()) 
        {
          ptw_datamodel->queue_psc_hit_metric[ptw_level]++;
          // hit at psc
          // get the next pt addr
          next_pt_addr = check_addr.value();
          // update to next level
          ptw_level = pscl->level-1; 
          // mix to lookup next level
          next_pt_addr = splice_bits(next_pt_addr, vmem.get_offset(handle_pkt.address, ptw_level) * PTE_BYTES, LOG2_PAGE_SIZE);
          miss_at_root = false;
        }
        else
        {
          ptw_datamodel->queue_psc_miss_metric[ptw_level]++;
          miss_at_root = true;
        }
      }
    }

    // auto ptw_addr = splice_bits(CR3_addr[cpu * KNOB_SMT_ENABLE + handle_pkt.thread_id], vmem.get_offset(handle_pkt.address, vmem.pt_levels - 1) * PTE_BYTES, LOG2_PAGE_SIZE);
    // auto ptw_level = vmem.pt_levels - 1;
    // for (auto pscl : {&PSCL5, &PSCL4, &PSCL3, &PSCL2}) {
    //   if (auto check_addr = pscl->check_hit(handle_pkt.address, handle_pkt.thread_id); check_addr.has_value()) {
    //     ptw_addr = check_addr.value();
    //     ptw_level = pscl->level - 1; 
    //   }
    // }

    PACKET packet = handle_pkt;
    packet.fill_level = lower_level->fill_level; // This packet will be sent from L1 to PTW.
    packet.address = next_pt_addr;
    packet.v_address = handle_pkt.address;
    packet.cpu = cpu;
    packet.type = TRANSLATION;
    packet.init_translation_level = ptw_level;
    packet.translation_level = packet.init_translation_level;
    packet.to_return = {this};
    packet.thread_id = handle_pkt.thread_id;

    int rq_index = lower_level->add_rq(&packet);
    if (rq_index == -2)
      return;

    // Track PTW
    if(track.stop == 0)
    {
      track.stop = 1;
      track.readmiss_address = handle_pkt.address;
      track.readmiss_v_address = handle_pkt.v_address;
      // cout << std::hex << track.readmiss_address << ", " << track.readmiss_v_address << ", req, " << packet.address << std::dec << '\n';
    }

    packet.to_return = handle_pkt.to_return; // Set the return for MSHR packet same as read packet.
    packet.type = handle_pkt.type;

    auto it = MSHR.insert(std::end(MSHR), packet);
    it->cycle_enqueued = current_cycle;
    it->event_cycle = std::numeric_limits<uint64_t>::max();

    RQ.pop_front();
    reads_this_cycle--;

    it->uv_cycle_enqueue = current_cycle;
  }
}

void PageTableWalker::handle_fill()
{
  int fill_this_cycle = MAX_FILL;

  while (fill_this_cycle > 0 && !std::empty(MSHR) && MSHR.front().event_cycle <= current_cycle) {
    auto fill_mshr = MSHR.begin();

    // Translation complete now remove MSHR entry, when translation level is 0
    if (fill_mshr->translation_level == 0) // If translation complete
    {
      // Return the translated physical address to STLB. Does not contain last
      // 12 bits
      auto [addr, fault] = vmem.va_to_pa(cpu*KNOB_SMT_ENABLE + fill_mshr->thread_id, fill_mshr->v_address);

      // // Track PTW starts from handle_read --> first enable there
      // if(track.stop && track.readmiss_v_address == fill_mshr->v_address)
      // {
      //   cout << (int)fill_mshr->translation_level<< ", " << std::hex << fill_mshr->address << ", " << addr << std::dec << '\n';
      // }

      if(KNOB_TTP==1)
      {
        llcObject->prefetch_line(addr, llcObject->fill_level, 1);
      }  

      // We dont have free frame availbale, hence minor fault.
      if (warmup_complete[cpu] && fault) 
      {
        fill_mshr->event_cycle = current_cycle + vmem.minor_fault_penalty;
        MSHR.sort(ord_event_cycle<PACKET>{});

        ptw_datamodel->page_fault[0]++;
      } 
      // Translation finally complete
      else 
      {
        fill_mshr->data = addr;
        fill_mshr->address = fill_mshr->v_address;

        DP(if (warmup_complete[packet->cpu]) {
          std::cout << "[" << NAME << "] " << __func__ << " instr_id: " << fill_mshr->instr_id;
          std::cout << " address: " << std::hex << (fill_mshr->address >> LOG2_PAGE_SIZE) << " full_addr: " << fill_mshr->address;
          std::cout << " full_v_addr: " << fill_mshr->v_address;
          std::cout << " data: " << fill_mshr->data << std::dec;
          std::cout << " translation_level: " << +fill_mshr->translation_level;
          std::cout << " index: " << std::distance(MSHR.begin(), fill_mshr) << " occupancy: " << get_occupancy(0, 0);
          std::cout << " event: " << fill_mshr->event_cycle << " current: " << current_cycle << std::endl;
        });

        for (auto ret : fill_mshr->to_return)
          ret->return_data(&(*fill_mshr));

        if (warmup_complete[cpu])
          total_miss_latency += current_cycle - fill_mshr->cycle_enqueued;

        MSHR.erase(fill_mshr);

        ptw_datamodel->packet_processed++;
        ptw_datamodel->packet_processed_total_miss_latency += total_miss_latency;

      }
    } 
    
    // simulating PSC failed lookup. For each of translation level penalty would be to access memory
    // Satrting with translation level between 5 to 1. Fill the incomming paddr suitable for corresponding translation level.
    // Then decrease the level and send read requests for same VA but at new translation level.

    else 
    {
      auto [addr, fault] = vmem.get_pte_pa(cpu*KNOB_SMT_ENABLE + fill_mshr->thread_id, fill_mshr->v_address, fill_mshr->translation_level);

      // // Track PTW handle_read --> first enable there
      // if(track.stop && track.readmiss_v_address == fill_mshr->v_address)
      // {
      //   cout << (int)fill_mshr->translation_level<< ", " << std::hex << fill_mshr->address << ", " << addr << std::dec << '\n';
      // }

      if (warmup_complete[cpu] && fault) 
      {
        fill_mshr->event_cycle = current_cycle + vmem.minor_fault_penalty;
        MSHR.sort(ord_event_cycle<PACKET>{});

        ptw_datamodel->page_fault[fill_mshr->translation_level]++;
      } 
      else 
      {
        // usercode
        ptw_datamodel->psc_level_packet_processed_miss_latency[fill_mshr->translation_level] += current_cycle - fill_mshr->uv_cycle_enqueue;

        if (fill_mshr->translation_level == PSCL5.level)
          PSCL5.fill_cache(addr, fill_mshr->v_address, fill_mshr->thread_id);
        if (fill_mshr->translation_level == PSCL4.level)
          PSCL4.fill_cache(addr, fill_mshr->v_address, fill_mshr->thread_id);
        if (fill_mshr->translation_level == PSCL3.level)
          PSCL3.fill_cache(addr, fill_mshr->v_address, fill_mshr->thread_id);
        if (fill_mshr->translation_level == PSCL2.level)
          PSCL2.fill_cache(addr, fill_mshr->v_address, fill_mshr->thread_id);

        DP(if (warmup_complete[packet->cpu]) {
          std::cout << "[" << NAME << "] " << __func__ << " instr_id: " << fill_mshr->instr_id;
          std::cout << " address: " << std::hex << (fill_mshr->address >> LOG2_PAGE_SIZE) << " full_addr: " << fill_mshr->address;
          std::cout << " full_v_addr: " << fill_mshr->v_address;
          std::cout << " data: " << fill_mshr->data << std::dec;
          std::cout << " translation_level: " << +fill_mshr->translation_level;
          std::cout << " index: " << std::distance(MSHR.begin(), fill_mshr) << " occupancy: " << get_occupancy(0, 0);
          std::cout << " event: " << fill_mshr->event_cycle << " current: " << current_cycle << std::endl;
        });


        bool miss_at_root = false;
        // search next level page table
        uint8_t ptw_level = fill_mshr->translation_level - 1;
        // use next 9bits with base addr of next level page table
        uint64_t next_pt_addr = splice_bits(addr, vmem.get_offset(fill_mshr->v_address, ptw_level) * PTE_BYTES, LOG2_PAGE_SIZE);
        // lookup this levels PSC, if found in PSC then update next_pt_addr, ptw_level and continue search
        for (auto pscl : {&PSCL5, &PSCL4, &PSCL3, &PSCL2}) 
        {
          if(ptw_level != pscl->level)
            continue;
          if (auto check_addr = pscl->check_hit(next_pt_addr, fill_mshr->thread_id); check_addr.has_value()) {
            ptw_datamodel->queue_psc_hit_metric[ptw_level]++;
            next_pt_addr = check_addr.value();
            ptw_level = pscl->level - 1; 
            next_pt_addr = splice_bits(next_pt_addr, vmem.get_offset(fill_mshr->v_address, ptw_level) * PTE_BYTES, LOG2_PAGE_SIZE);
            miss_at_root = false;
            
          }
          else
          {
            ptw_datamodel->queue_psc_miss_metric[ptw_level]++;
            miss_at_root = true;
          }
        }

        PACKET packet = *fill_mshr;
        packet.cpu = cpu;
        packet.type = TRANSLATION;
        packet.address = next_pt_addr;
        packet.to_return = {this};
        packet.translation_level = fill_mshr->translation_level - 1;
        packet.thread_id = fill_mshr->thread_id;

        int rq_index = lower_level->add_rq(&packet);
        if (rq_index != -2) 
        {
          fill_mshr->event_cycle = std::numeric_limits<uint64_t>::max();
          fill_mshr->address = packet.address;
          fill_mshr->translation_level--;

          MSHR.splice(std::end(MSHR), MSHR, fill_mshr);

          // usercode
          ptw_datamodel->psc_level_packet_processed[packet.translation_level]++;
          fill_mshr->uv_cycle_enqueue = current_cycle;
        }
      }
    }
    fill_this_cycle--;
  }
}

void PageTableWalker::operate()
{
  handle_fill();
  handle_read();
  RQ.operate();
}

int PageTableWalker::add_rq(PACKET* packet)
{
  ptw_datamodel->queue_basic_metric[Basic::REQUESTED]++;
  assert(packet->address != 0);

  // check for duplicates in the read queue
  auto found_rq = std::find_if(RQ.begin(), RQ.end(), eq_addr<PACKET>(packet->address, LOG2_PAGE_SIZE));
  // assert(found_rq == RQ.end()); // Duplicate request should not be sent.
  
  if(found_rq != RQ.end())
  {
    ptw_datamodel->queue_basic_metric[Basic::MERGED]++;
    // earlier assertion fail
  }

  // check occupancy
  if (RQ.full()) {
    
    ptw_datamodel->queue_basic_metric[Basic::REJECTED]++;
    return -2; // cannot handle this request
  }

  // if there is no duplicate, add it to RQ
  RQ.push_back(*packet);

  ptw_datamodel->queue_basic_metric[Basic::ADDED]++;
  return RQ.occupancy();
}

void PageTableWalker::return_data(PACKET* packet)
{
  for (auto& mshr_entry : MSHR) {
    if (eq_addr<PACKET>{packet->address, LOG2_BLOCK_SIZE}(mshr_entry)) {
      mshr_entry.event_cycle = current_cycle;

      DP(if (warmup_complete[cpu]) {
        std::cout << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << mshr_entry.instr_id;
        std::cout << " address: " << std::hex << mshr_entry.address;
        std::cout << " v_address: " << mshr_entry.v_address;
        std::cout << " data: " << mshr_entry.data << std::dec;
        std::cout << " translation_level: " << +mshr_entry.translation_level;
        std::cout << " occupancy: " << get_occupancy(0, mshr_entry.address);
        std::cout << " event: " << mshr_entry.event_cycle << " current: " << current_cycle << std::endl;
      });
    }
  }

  MSHR.sort(ord_event_cycle<PACKET>());
}

uint32_t PageTableWalker::get_occupancy(uint8_t queue_type, uint64_t address)
{
  if (queue_type == 0)
    return std::count_if(MSHR.begin(), MSHR.end(), is_valid<PACKET>());
  else if (queue_type == 1)
    return RQ.occupancy();
  return 0;
}

uint32_t PageTableWalker::get_size(uint8_t queue_type, uint64_t address)
{
  if (queue_type == 0)
    return MSHR_SIZE;
  else if (queue_type == 1)
    return RQ.size();
  return 0;
}

void PagingStructureCache::fill_cache(uint64_t next_level_paddr, uint64_t vaddr, int thread_id)
{
  auto set_idx = (vaddr >> vmem.shamt(level + 1)) & bitmask(lg2(NUM_SET));
  auto set_begin = std::next(std::begin(block), set_idx * NUM_WAY);
  auto set_end = std::next(set_begin, NUM_WAY);
  auto fill_block = std::max_element(set_begin, set_end, lru_comparator<block_t, block_t>());

  *fill_block = {true, vaddr, next_level_paddr, fill_block->lru, thread_id};
  std::for_each(set_begin, set_end, lru_updater<block_t>(fill_block));
}

std::optional<uint64_t> PagingStructureCache::check_hit(uint64_t address, int thread_id)
{
  auto set_idx = (address >> vmem.shamt(level + 1)) & bitmask(lg2(NUM_SET));
  auto set_begin = std::next(std::begin(block), set_idx * NUM_WAY);
  auto set_end = std::next(set_begin, NUM_WAY);
  auto hit_block = std::find_if(set_begin, set_end, eq_addr<block_t>{address, vmem.shamt(level + 1)});

  if (hit_block != set_end)
  {
    if(hit_block->thread_id == thread_id)
    {
      return splice_bits(hit_block->data, vmem.get_offset(address, level) * PTE_BYTES, LOG2_PAGE_SIZE);
    }
  }

  return {};
}

void PageTableWalker::print_deadlock()
{
  if (!std::empty(MSHR)) {
    std::cout << NAME << " MSHR Entry" << std::endl;
    std::size_t j = 0;
    for (PACKET entry : MSHR) {
      std::cout << "[" << NAME << " MSHR] entry: " << j++ << " instr_id: " << entry.instr_id;
      std::cout << " address: " << std::hex << entry.address << " v_address: " << entry.v_address << std::dec << " type: " << +entry.type;
      std::cout << " translation_level: " << +entry.translation_level;
      std::cout << " fill_level: " << +entry.fill_level << " event_cycle: " << entry.event_cycle << std::endl;
    }
  } else {
    std::cout << NAME << " MSHR empty" << std::endl;
  }
}
