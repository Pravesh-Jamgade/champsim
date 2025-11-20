#include "ptw.h"

#include "champsim.h"
#include "util.h"
#include "vmem.h"
#include "DataModel.h"

#include "cache.h"
#include "victima.h"
#include "pagetable.h"
// Extra configguration
extern int KNOB_TTP;
extern int KNOB_SMT_ENABLE;
extern int KNOB_PSCL_ROOT_LEVEL;
extern int KNOB_ENABLE_MFOE_V2;
#define PSC_READ_LATENCY 2

extern map<tuple<uint64_t, int>, PTWC> ptw_pred;

extern VirtualMemory vmem;
extern uint8_t warmup_complete[NUM_CPUS];
extern ProcessPageTable* process_page_table;
extern uint64_t POM_CPU_KEY;

PageTableWalker::PageTableWalker(string v1, uint32_t cpu, unsigned fill_level, uint32_t v2, uint32_t v3, uint32_t v4, uint32_t v5, uint32_t v6, uint32_t v7,
                                 uint32_t v8, uint32_t v9, uint32_t v10, uint32_t v11, uint32_t v12, uint32_t v13, unsigned latency, MemoryRequestConsumer* ll, CACHE* llc)
    : champsim::operable(1, v1), MemoryRequestConsumer(fill_level), MemoryRequestProducer(ll), NAME(v1), cpu(cpu), MSHR_SIZE(v11), MAX_READ(v12),
      MAX_FILL(v13), RQ{v10, latency}, PSCL5{"PSCL5", 4, v2, v3}, // Translation from L5->L4
      PSCL4{"PSCL4", 3, v4, v5},                                  // Translation from L5->L3
      PSCL3{"PSCL3", 2, v6, v7},                                  // Translation from L5->L2
      PSCL2{"PSCL2", 1, v8, v9},                                  // Translation from L5->L1
      llcObject(llc)
{

  debugLog = logger(false);
  dlog = logger(false);
  ptw_datamodel = new PTWDataModel(cpu);
  fill_counters.resize(5);

  // supporting 16 threads
  for(int i=0; i< 16; i++)
  {
    auto entry = vmem.func_allocate_page();
    CR3_addr.push_back(entry);
  }

}

void PageTableWalker::_overwrite()
{
  //KNOB_PSCL_ROOT_LEVEL tells the level_number of ROOT
  // vmem.pt_levels tells number of levels hence we start with vmem.pt_levels-1 for level_number of root
  if(KNOB_PSCL_ROOT_LEVEL != vmem.pt_levels)
  {
    cout << "Overwrite Setting, "<< NAME <<", PTW-levels= " <<KNOB_PSCL_ROOT_LEVEL<<'\n';
    vmem.pt_levels = KNOB_PSCL_ROOT_LEVEL;
  }

  pscl_array.push_back(&PSCL2);
  pscl_array.push_back(&PSCL3);
  pscl_array.push_back(&PSCL4);
  pscl_array.push_back(&PSCL5);

  while(pscl_array.back()->level != KNOB_PSCL_ROOT_LEVEL)
  {
    pscl_array.pop_back();
  }

  pscl_array.reverse();

  std::random_device rd;
  std::mt19937_64 engine(rd());
  std::uniform_int_distribution<uint64_t> dist;

  // supporting 16 threads
  for(int i=0; i< 16; i++)
  {
    uint64_t random_64bit_num = dist(engine);
    uint64_t mask = (1ULL << 48) - 1;
    random_64bit_num = random_64bit_num & mask;
    asid.push_back(random_64bit_num);
  }

  POMTLB_baseaddr = vmem.func_allocate_page();
  POM_CPU_KEY = (POMTLB_baseaddr >> LOG2_PAGE_SIZE) & ((1ULL << 16) - 1);
  cout << NAME << ", POMTLB_baseaddr= " << std::hex << POMTLB_baseaddr << std::dec << ", POM_CPU_KEY= " << POM_CPU_KEY << '\n';
}

void PageTableWalker::handle_read()
{
  int reads_this_cycle = MAX_READ;

  while (reads_this_cycle > 0 && RQ.has_ready() && std::size(MSHR) != MSHR_SIZE) 
  {
    PACKET& handle_pkt = RQ.front();

    if(handle_pkt.psc_state == PSC_STATE::QUEUED)
    {
      handle_pkt.psc_state = PSC_STATE::STALL;
      handle_pkt.event_cycle = current_cycle + PSC_READ_LATENCY;
      RQ.sort(ord_event_cycle<PACKET>{});
      reads_this_cycle--;
      continue;
    }

    debugLog.log(current_cycle, NAME, "Retry-With-PTW", intToHex(handle_pkt.address), intToHex(handle_pkt.v_address), "pom", handle_pkt.pomflag[POM::POM], "pommiss", handle_pkt.pomflag[POM::POM_MISS], "pom_to_ptw", handle_pkt.pomflag[POM::POM_TO_PTW], "pom_to_ptw_fini", handle_pkt.pomflag[POM::POM_TO_PTW_FINI], '\n');

    // CR3_addr.push_back(vmem.get_pte_pa(cpu * KNOB_SMT_ENABLE + handle_pkt.thread_id, 0, vmem.pt_levels).first);

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
    uint32_t ptw_level = vmem.pt_levels;
    // first pa to start page table walk
    // shift amount is 27 for 4-th-level. How ? --> 9 * (4-1) = 27 --> i.e [9 * (curr_level-1)]
    uint64_t next_pt_addr = splice_bits(CR3_addr[cpu * KNOB_SMT_ENABLE + handle_pkt.thread_id], vmem.get_offset(handle_pkt.address, ptw_level-1) * PTE_BYTES, LOG2_PAGE_SIZE);

    // std::cout <<std::hex<< "PTW handle_read, " << CR3_addr[cpu * KNOB_SMT_ENABLE + handle_pkt.thread_id] << ", " << (vmem.get_offset(handle_pkt.address, ptw_level-1) * PTE_BYTES) << "="<< next_pt_addr <<std::dec<< "\n";
    bool miss_at_root = true;

    if(0)
    {
      // optimized
      for (auto pscl : pscl_array) {
        if (auto check_addr = pscl->check_hit(next_pt_addr, handle_pkt.v_address, handle_pkt.thread_id); check_addr.has_value()) {
          next_pt_addr = check_addr.value();
          ptw_level = pscl->level - 1; 
        }
      }
    }
    else
    {
      //detailed
      // look for this levels PSC, if corresponding entry found then we can skip the memory access for this level
      for (auto pscl : pscl_array) 
      {
        if(ptw_level != pscl->level)
          continue;
       
        if (auto check_addr = pscl->check_hit(next_pt_addr, handle_pkt.v_address, handle_pkt.thread_id); check_addr.has_value()) 
        {
          // if(handle_pkt.vflag[victima_stlbevict_ptw])
          debugLog.log("PSC-Hit", current_cycle, "level-"+to_string(ptw_level),"VP", intToHex(handle_pkt.v_address), "next_pte_addr", intToHex(next_pt_addr), "instr", handle_pkt.instr_id, "t", handle_pkt.thread_id, '\n');

          miss_at_root = false;
          ptw_datamodel->queue_psc_hit_metric[ptw_level]++;
          // hit at psc
          // get the next pt addr
          uint64_t current_pte_address = next_pt_addr;
          next_pt_addr = check_addr.value();
          // update to next level
          ptw_level = ptw_level-1;

          if(ptw_level > 0)
          {
            // mix to lookup next level
            next_pt_addr = splice_bits(next_pt_addr, vmem.get_offset(handle_pkt.address, ptw_level-1) * PTE_BYTES, LOG2_PAGE_SIZE);
          }
          else if(ptw_level == 0)
          {
            // assuming it never go to cache hierarch and completes walk witin PSCs then 0 dram but 1 PTW 
            // PTE address of leaf PT
            victima_update(handle_pkt.v_address, handle_pkt.cpu*KNOB_SMT_ENABLE+handle_pkt.thread_id , PTW_Freq);
            handle_pkt.data = next_pt_addr;
            // found data page
            for(auto ret: handle_pkt.to_return)
            {
              ret->return_data(&handle_pkt);
            }
            break;
          }
        }
        else
        {
          ptw_datamodel->queue_psc_miss_metric[ptw_level]++;
          miss_at_root = true;
          break;
        }
      }
    }

    if(miss_at_root)
    {
      PACKET packet = handle_pkt;
      packet.page_table_base_address = CR3_addr[cpu * KNOB_SMT_ENABLE + handle_pkt.thread_id];
      packet.fill_level = lower_level->fill_level; // This packet will be sent from L1 to PTW.
      packet.address = next_pt_addr;
      packet.v_address = handle_pkt.address;
      packet.cpu = cpu;
      packet.type = TRANSLATION;
      packet.init_translation_level = ptw_level;
      packet.translation_level = packet.init_translation_level;
      packet.to_return = {this};
      packet.thread_id = handle_pkt.thread_id;
      packet.pom_address = handle_pkt.pom_address;
      // needed at DRAM to store POM-entry
      packet.pomflag[POM::POM_TO_PTW] = (ptw_level == 1 && handle_pkt.pomflag[POM::POM_TO_PTW]);

      // "victima_stlbevict_ptw" we are updating it here, the same packet will be use as MSHR entry. For next iteration of PTW, we would need to use same flag value. Hence test the ptw_level and flag.
      packet.vflag[VF::victima_stlbevict_ptw] = (ptw_level == 1 && handle_pkt.vflag[VF::victima_stlbevict_ptw]);
  
      int rq_index = lower_level->add_rq(&packet);
      if (rq_index == -2)
        return;
      
      // // if(handle_pkt.vflag[victima_stlbevict_ptw])
      debugLog.log("PTW-sent", current_cycle,  "level-"+to_string(ptw_level),"VP", intToHex(packet.v_address), "next_pte_addr", intToHex(packet.address), "instr", handle_pkt.instr_id, "t", handle_pkt.thread_id, "pomtoptw", packet.pomflag[POM::POM_TO_PTW], "original_pom2ptw", handle_pkt.pomflag[POM::POM_TO_PTW], '\n');

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
      packet.psc_state = PSC_STATE::QUEUED;
      
      // "victima_stlbevict_ptw" we are updating it here, the same packet will be use as MSHR entry. For next iteration of PTW, we would need to use same flag value. Hence test the ptw_level and flag.
      packet.vflag[VF::victima_stlbevict_ptw] = handle_pkt.vflag[VF::victima_stlbevict_ptw];
      packet.pomflag[POM::POM_TO_PTW] = handle_pkt.pomflag[POM::POM_TO_PTW];
      
      auto it = MSHR.insert(std::end(MSHR), packet);
      it->cycle_enqueued = current_cycle;
      it->event_cycle = std::numeric_limits<uint64_t>::max();

      it->uv_cycle_enqueue = current_cycle;
    }
    
    RQ.pop_front();
    reads_this_cycle--;
  }
}

void PageTableWalker::handle_fill()
{
  int fill_this_cycle = MAX_FILL;

  while (fill_this_cycle > 0 && !std::empty(MSHR) && MSHR.front().event_cycle <= current_cycle) {
    auto fill_mshr = MSHR.begin();
    
    //TODO: add search cost
    if(fill_mshr->psc_state == PSC_STATE::QUEUED)
    {
      fill_mshr->psc_state = PSC_STATE::STALL;
      fill_mshr->event_cycle = current_cycle + PSC_READ_LATENCY;
      MSHR.sort(ord_event_cycle<PACKET>{});
      fill_this_cycle--;
      continue;
    }

    // Translation complete now remove MSHR entry, when translation level is 0
    if (fill_mshr->translation_level == 0) // If translation complete
    {
      uint64_t addr = fill_mshr->data;
      // fault is taken care at DRAM
      bool fault = fill_mshr->page_fault;

      if(KNOB_TTP==1)
      {
        llcObject->prefetch_line(addr, llcObject->fill_level, 1);
      }  
      
      //// Translation finally complete
      {
        // PTE address of leaf PT
        victima_update(fill_mshr->v_address, fill_mshr->cpu*KNOB_SMT_ENABLE+fill_mshr->thread_id , PTW_Freq);

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
      uint64_t addr = 0;
      bool fault = false;
      
      // packet return from PTW, now we will fill into specific PSC.
      // We also check if its fault if so then we add stall latency, which causes our packet to return again, hence next time we will use "else" block.
      // Once fill is done, we move to search next level, that time again we want to avoid entering to "if" block
      if(fill_mshr->state == State::PTW_FILL)
      {
        addr = fill_mshr->data;
        fault = fill_mshr->page_fault;
      }
      else
      {
        addr = fill_mshr->address;
      }

      if (warmup_complete[cpu] && fault) 
      {
        // reset page_fault flag, to prevent from entering again in stall period and move to fill PSC
        fill_mshr->page_fault = false;
        fill_mshr->event_cycle = current_cycle + (KNOB_ENABLE_MFOE_V2 ? PSC_READ_LATENCY : vmem.minor_fault_penalty);
        MSHR.sort(ord_event_cycle<PACKET>{});

        ptw_datamodel->page_fault[(int)fill_mshr->translation_level]++;
      } 
      else 
      {
        DP(if (warmup_complete[packet->cpu]) {
          std::cout << "[" << NAME << "] " << __func__ << " instr_id: " << fill_mshr->instr_id;
          std::cout << " address: " << std::hex << (fill_mshr->address >> LOG2_PAGE_SIZE) << " full_addr: " << fill_mshr->address;
          std::cout << " full_v_addr: " << fill_mshr->v_address;
          std::cout << " data: " << fill_mshr->data << std::dec;
          std::cout << " translation_level: " << +fill_mshr->translation_level;
          std::cout << " index: " << std::distance(MSHR.begin(), fill_mshr) << " occupancy: " << get_occupancy(0, 0);
          std::cout << " event: " << fill_mshr->event_cycle << " current: " << current_cycle << std::endl;
        });

        // usercode
        ptw_datamodel->psc_level_packet_processed_miss_latency[fill_mshr->translation_level] += current_cycle - fill_mshr->uv_cycle_enqueue;

        if(fill_mshr->state == State::PTW_FILL)
        {
          debugLog.log("PTW-fill", current_cycle, "level-"+to_string((int)fill_mshr->translation_level),"VP", intToHex(fill_mshr->v_address), "next_pte_addr", intToHex(addr), "instr", fill_mshr->instr_id, "t", fill_mshr->thread_id, "hw", hit_where_str[fill_mshr->hit_where], "ori_pom2ptw", fill_mshr->pomflag[POM::POM_TO_PTW], '\n');

          ptw_datamodel->matrix_cache_to_ptwlevel_hits[fill_mshr->translation_level][fill_mshr->hit_where]++;
          fill_counters[fill_mshr->translation_level]++;
          
          if (fill_mshr->translation_level == PSCL5.level)
            PSCL5.fill_cache(addr, fill_mshr->v_address, fill_mshr->thread_id);
          if (fill_mshr->translation_level == PSCL4.level)
            PSCL4.fill_cache(addr, fill_mshr->v_address, fill_mshr->thread_id);
          if (fill_mshr->translation_level == PSCL3.level)
            PSCL3.fill_cache(addr, fill_mshr->v_address, fill_mshr->thread_id);
          if (fill_mshr->translation_level == PSCL2.level)
            PSCL2.fill_cache(addr, fill_mshr->v_address, fill_mshr->thread_id);
          
          victima_update(fill_mshr->v_address, fill_mshr->cpu*KNOB_SMT_ENABLE+fill_mshr->thread_id, PageFeature::PTW_Cost, fill_mshr->hit_where);
          
          fill_mshr->state = State::PSC_Search;
          // baseaddress of next_level_pt
          fill_mshr->address = addr;
          // order of line imp: level=1 becomes level=0 and hence it will notify end of PTW and allocate data_page (minor-fault) if not exists
          fill_mshr->translation_level = fill_mshr->translation_level - 1;
        }
        else if(fill_mshr->state == State::PSC_Search)
        {
          bool miss_in_psc = false;
          // search next level page table
          uint8_t ptw_level = fill_mshr->translation_level;
          // use next 9bits with base addr of next level page table
          uint64_t next_pt_addr = splice_bits(addr, vmem.get_offset(fill_mshr->v_address, ptw_level-1) * PTE_BYTES, LOG2_PAGE_SIZE);

          // lookup this levels PSC, if found in PSC then update next_pt_addr, ptw_level and continue search
          for (auto pscl : pscl_array) 
          {
            if(ptw_level != pscl->level)
              continue;
            
            if (auto check_addr = pscl->check_hit(next_pt_addr, fill_mshr->v_address, fill_mshr->thread_id); check_addr.has_value()) 
            {
              // if(fill_mshr->vflag[victima_stlbevict_ptw])
              debugLog.log( "PSC-Hit", current_cycle,"level-"+to_string(ptw_level), "VP", intToHex(fill_mshr->v_address), "next_pte_addr", intToHex(next_pt_addr), "instr", fill_mshr->instr_id, "t", fill_mshr->thread_id, '\n');
              ptw_datamodel->queue_psc_hit_metric[ptw_level]++;
              next_pt_addr = check_addr.value();

              // order important example: PTW_FILL for level=3 , it becomes level=2, if hit in level=2, we need to search level=1 next
              // exmplae (conti.): if it is miss-here then we use level=2 and send a memory request
              ptw_level = ptw_level - 1;

              break;
            }
            else
            {
              miss_in_psc = true;
              ptw_datamodel->queue_psc_miss_metric[ptw_level]++;
              break;
            }
          }

          fill_mshr->address = next_pt_addr;
          fill_mshr->translation_level = ptw_level;
          fill_mshr->psc_state = PSC_STATE::QUEUED;

          if(miss_in_psc)
          {
            PACKET packet = *fill_mshr;
            packet.cpu = cpu;
            packet.type = TRANSLATION;
            packet.address = next_pt_addr;
            packet.to_return = {this};
            packet.translation_level = ptw_level;
            packet.thread_id = fill_mshr->thread_id;
            packet.vflag[VF::victima_stlbevict_ptw] = (ptw_level==1 && fill_mshr->vflag[VF::victima_stlbevict_ptw]);// if level=1 then only translation cache-block to tlb-block at L2
            packet.psc_state = PSC_STATE::QUEUED;
            packet.page_table_base_address = addr;
            packet.pom_address = fill_mshr->pom_address;
            // needed at DRAM to store POM-entry
            packet.pomflag[POM::POM_TO_PTW] = (ptw_level == 1 && fill_mshr->pomflag[POM::POM_TO_PTW]);

            int rq_index = lower_level->add_rq(&packet);
            if (rq_index != -2) 
            {
              // if(fill_mshr->vflag[victima_stlbevict_ptw])
              debugLog.log("PTW-sent", current_cycle, "level-"+to_string(ptw_level),"VA", intToHex(fill_mshr->v_address), "next_pte_addr", intToHex(next_pt_addr), "instr", fill_mshr->instr_id, "t", fill_mshr->thread_id, "pom_to_ptw", packet.pomflag[POM::POM_TO_PTW], "original_pom2ptw", fill_mshr->pomflag[POM::POM_TO_PTW], '\n');
              fill_mshr->event_cycle = std::numeric_limits<uint64_t>::max();
              fill_mshr->page_table_base_address = addr;

              MSHR.splice(std::end(MSHR), MSHR, fill_mshr);
  
              // usercode
              ptw_datamodel->psc_level_packet_processed[packet.translation_level]++;
              fill_mshr->uv_cycle_enqueue = current_cycle;
            }
          }
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
  auto found_rq = std::find_if(RQ.begin(), RQ.end(), eq_addr<PACKET>(packet->address, LOG2_PAGE_SIZE, packet->thread_id, false));
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
    if (eq_addr<PACKET>{packet->address, LOG2_BLOCK_SIZE, packet->thread_id, 1}(mshr_entry)) {
      // PTW: added PSC write cost
      mshr_entry.event_cycle = current_cycle + 1;
      mshr_entry.state = State::PTW_FILL;
      mshr_entry.hit_where = packet->hit_where;
      mshr_entry.page_fault = packet->page_fault;
      mshr_entry.data = packet->data;

      mshr_entry.pomflag[POM::POM_TO_PTW_FINI] = packet->pomflag[POM::POM_TO_PTW];
      
      DP(if (warmup_complete[cpu]) {
        std::cout << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << mshr_entry.instr_id;
        std::cout << " address: " << std::hex << mshr_entry.address;
        std::cout << " v_address: " << mshr_entry.v_address;
        std::cout << " data: " << mshr_entry.data << std::dec;
        std::cout << " translation_level: " << +mshr_entry.translation_level;
        std::cout << " occupancy: " << get_occupancy(0, mshr_entry.address);
        std::cout << " event: " << mshr_entry.event_cycle << " current: " << current_cycle << std::endl;
      });

      // track hit where for each at these structure
      ptw_datamodel->readmiss_hitwhere[mshr_entry.hit_where]++;
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
  // assert(thread_id != -1);
  if(thread_id == -1)
  {
    cout << "fill_cache @ ptw.cc failed\n";
    exit(0);
  }
  auto set_idx = (vaddr >> vmem.shamt(level - 1)) & bitmask(lg2(NUM_SET));
  auto set_begin = std::next(std::begin(block), set_idx * NUM_WAY);
  auto set_end = std::next(set_begin, NUM_WAY);
  auto fill_block = std::max_element(set_begin, set_end, lru_comparator<block_t, block_t>());

  *fill_block = {true, vaddr, next_level_paddr, fill_block->lru, thread_id};
  std::for_each(set_begin, set_end, lru_updater<block_t>(fill_block));
}

std::optional<uint64_t> PagingStructureCache::check_hit(uint64_t address, uint64_t vaddr, int thread_id)
{
  // assert(thread_id != -1);
  if(thread_id == -1)
  {
    cout << "check_hit @ ptw.cc failed\n";
    exit(0);
  }
  /*reason: cache_fill uses indexing from vaddr, then check_hit must use virtual address bits embedded in page_offset_part of generated phys.address
  [page_nuber|**9bit_offset**|PTE_offset] --> extract from next_pte_base i.e. address*/
  auto set_idx = (vaddr >> vmem.shamt(level - 1)) & bitmask(lg2(NUM_SET));
  auto set_begin = std::next(std::begin(block), set_idx * NUM_WAY);
  auto set_end = std::next(set_begin, NUM_WAY);
  auto hit_block = std::find_if(set_begin, set_end, eq_addr<block_t>{address, vmem.shamt(level - 1)});

  if (hit_block != set_end)
  {
    if(hit_block->thread_id == thread_id)
    {
      return splice_bits(hit_block->data, vmem.get_offset(address, level - 1) * PTE_BYTES, LOG2_PAGE_SIZE);
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

  if(!empty(RQ))
  {
    cout << NAME << " RQ Entry" << '\n';
    for(auto entry: RQ)
    {
      cout << std::hex << entry.address <<", " <<entry.v_address << ", level, " << entry.translation_level << ", init-level, " << entry.init_translation_level << ", instr, " << entry.instr_id << '\n';
    } 
  }
}

void PageTableWalker::victima_update(uint64_t vaddr, int cpu, int signal, int hit_where)
{
  uint64_t key = vaddr >> (LOG2_PAGE_SIZE);
  tuple<uint64_t, int> completeKey{key, cpu};

  auto found = ptw_pred.find(completeKey);

  if(found == ptw_pred.end())
  {
    ptw_pred[completeKey] = {0,0,0};
  }

  // freq: how many times PTW is initiated ?
  if(signal == PageFeature::PTW_Freq)
  {
    ptw_pred[completeKey].freq+=1;
  }
  // cost: how many times PTW has accessed DRAM ?
  else if(signal == PageFeature::PTW_Cost && hit_where == CACHE_ID::IS_DRAM)
  {
    ptw_pred[completeKey].cost+= 1;
  }
}
