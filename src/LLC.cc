#include "LLC.h"
#include "cache.h"
extern vector<CACHE*> banks;

LLC::LLC()
{

}

uint32_t LLC::get_set(uint64_t address)
{
  return (uint32_t)(address & ((1 << lg2(LLC_SET)) - 1));
}

int LLC::add_rq(PACKET *packet)
{
  // cout << "cycle, " << current_core_cycle[packet->cpu] << ", " << NAME << ", RQ, "  << packet->address << "\n";

  int index = RQ.head;
  uint32_t set = get_set(RQ.entry[index].address);
  uint32_t bank_no = get_bank_no(set);
  return banks[bank_no]->add_rq(packet);
}

int LLC::add_wq(PACKET *packet)
{
  // cout << "cycle, " << current_core_cycle[packet->cpu] << ", " << NAME << ", WQ, "  << packet->address << "\n";

  int index = WQ.head;
  uint32_t set = get_set(WQ.entry[index].address);
  uint32_t bank_no = get_bank_no(set);
  return banks[bank_no]->add_wq(packet);
}

int LLC::add_pq(PACKET* packet)
{

}

void LLC::return_data(PACKET* packet)
{
  int mshr_index = check_mshr(packet);

  // sanity check
  if (mshr_index == -1)
  {
    cerr << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << packet->instr_id << " cannot find a matching entry!";
    cerr << " full_addr: " << hex << packet->full_addr;
    cerr << " address: " << packet->address << dec;
    cerr << " event: " << packet->event_cycle << " current: " << current_core_cycle[packet->cpu] << endl;
    assert(0);
  }

  // MSHR holds the most updated information about this request
  // no need to do memcpy
  MSHR.num_returned++;
  MSHR.entry[mshr_index].returned = COMPLETED;
  MSHR.entry[mshr_index].data = packet->data;
  MSHR.entry[mshr_index].pf_metadata = packet->pf_metadata;

  // ADD LATENCY
  if (MSHR.entry[mshr_index].event_cycle < current_core_cycle[packet->cpu])
    MSHR.entry[mshr_index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
  else
    MSHR.entry[mshr_index].event_cycle += LATENCY;

  update_fill_cycle();

  DP(if (warmup_complete[packet->cpu]) {
    cout << "[" << NAME << "_MSHR] " <<  __func__ << " instr_id: " << MSHR.entry[mshr_index].instr_id;
    cout << " address: " << hex << MSHR.entry[mshr_index].address << " full_addr: " << MSHR.entry[mshr_index].full_addr;
    cout << " data: " << MSHR.entry[mshr_index].data << dec << " num_returned: " << MSHR.num_returned;
    cout << " index: " << mshr_index << " occupancy: " << MSHR.occupancy;
    cout << " event: " << MSHR.entry[mshr_index].event_cycle << " current: " << current_core_cycle[packet->cpu] << " next: " << MSHR.next_fill_cycle << endl; });

}

void LLC::operate()
{
  for(int i=0; i< MSHR.SIZE; i++)
  {
    if(MSHR.entry[i].returned == COMPLETED)
    {
      if(get_occupancy(2, MSHR.entry[i].address) 
          != get_size(2, MSHR.entry[i].address))
      {
        // cout << "cycle, " << current_core_cycle << ", " << NAME << ", MSHR->WQ, " << MSHR.entry[i].address << '\n'; 
        add_wq(&MSHR.entry[i]);
        MSHR.remove_queue(&MSHR.entry[i]);
      }
    }
  }
}

void LLC::increment_WQ_FULL(uint64_t addr)
{

}

uint32_t LLC::get_occupancy(uint8_t queue_type, uint64_t addr)
{
  int index = RQ.head;
  uint32_t set = get_set(RQ.entry[index].address);
  uint32_t bank_no = get_bank_no(set);

  if (queue_type == 0)
    return MSHR.occupancy;
  else if (queue_type == 1)
    return banks[bank_no]->RQ.occupancy;
  else if (queue_type == 2)
    return banks[bank_no]->WQ.occupancy;
  else if (queue_type == 3)
    return banks[bank_no]->PQ.occupancy;

  return 0;
}

uint32_t LLC::get_size(uint8_t queue_type, uint64_t addr)
{
  int index = RQ.head;
  uint32_t set = get_set(RQ.entry[index].address);
  uint32_t bank_no = get_bank_no(set);

  if (queue_type == 0)
    return MSHR.SIZE;
  else if (queue_type == 1)
    return banks[bank_no]->RQ.SIZE;
  else if (queue_type == 2)
    return banks[bank_no]->WQ.SIZE;
  else if (queue_type == 3)
    return banks[bank_no]->PQ.SIZE;

  return 0;
}

void LLC::add_mshr(PACKET *packet)
{
  cout << std::hex << "cycle, " << current_core_cycle[packet->cpu] << ", " << NAME << ", Add_MSHR, "  << packet->address << "\n";

  uint32_t index = 0;
  packet->cycle_enqueued = current_core_cycle[packet->cpu];
  for (index = 0; index < MSHR_SIZE; index++)
  {
    if (MSHR.entry[index].address == 0)
    {

      MSHR.entry[index] = *packet;
      MSHR.entry[index].returned = INFLIGHT;
      MSHR.occupancy++;

      DP(if (warmup_complete[packet->cpu]) {
            cout << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << packet->instr_id;
            cout << " address: " << hex << packet->address << " full_addr: " << packet->full_addr << dec;
            cout << " index: " << index << " occupancy: " << MSHR.occupancy << endl; });

      break;
    }
  }
}


int LLC::check_mshr(PACKET *packet)
{
  for (uint32_t index = 0; index < MSHR_SIZE; index++)
  {
    if (MSHR.entry[index].address == packet->address)
    {
      DP(if (warmup_complete[packet->cpu]) {
		  cout << "[" << NAME << "_MSHR] " << __func__ << " same entry instr_id: " << packet->instr_id << " prior_id: " << MSHR.entry[index].instr_id;
		  cout << " address: " << hex << packet->address;
		  cout << " full_addr: " << packet->full_addr << dec << endl; });

      return index;
    }
  }
  return -1;
}

uint32_t LLC::get_bank_no(uint32_t set)
{
  uint32_t set_bits=log2(LLC_SET);
  uint32_t n=set_bits;
  uint32_t BANK_BIT=log2(NUM_BANKS);
  uint32_t mask=~(0);
  for(int i=0;i<BANK_BIT;i++)
  {
      n--;
      mask=mask & ~(1 << n);
      
  }

  unsigned int unset_set = set & mask;
  // unsigned int given_bits = cpu; 
  
  // unsigned int set_num = unset_set | (given_bits << set_bits-BANK_BIT );
  
  return unset_set;//set_num;
}

void LLC::update_fill_cycle()
{
  // update next_fill_cycle
  uint64_t min_cycle = UINT64_MAX;
  uint32_t min_index = MSHR.SIZE;
  for (uint32_t i = 0; i < MSHR.SIZE; i++)
  {
    if ((MSHR.entry[i].returned == COMPLETED) && (MSHR.entry[i].event_cycle < min_cycle))
    {
      min_cycle = MSHR.entry[i].event_cycle;
      min_index = i;
    }

    DP(if (warmup_complete[MSHR.entry[i].cpu]) {
        cout << "[" << NAME << "_MSHR] " <<  __func__ << " checking instr_id: " << MSHR.entry[i].instr_id;
        cout << " address: " << hex << MSHR.entry[i].address << " full_addr: " << MSHR.entry[i].full_addr;
        cout << " data: " << MSHR.entry[i].data << dec << " returned: " << +MSHR.entry[i].returned << " fill_level: " << MSHR.entry[i].fill_level;
        cout << " index: " << i << " occupancy: " << MSHR.occupancy;
        cout << " event: " << MSHR.entry[i].event_cycle << " current: " << current_core_cycle[MSHR.entry[i].cpu] << " next: " << MSHR.next_fill_cycle << endl; });
  }

  MSHR.next_fill_cycle = min_cycle;
  MSHR.next_fill_index = min_index;
  if (min_index < MSHR.SIZE)
  {

    DP(if (warmup_complete[MSHR.entry[min_index].cpu]) {
        cout << "[" << NAME << "_MSHR] " <<  __func__ << " instr_id: " << MSHR.entry[min_index].instr_id;
        cout << " address: " << hex << MSHR.entry[min_index].address << " full_addr: " << MSHR.entry[min_index].full_addr;
        cout << " data: " << MSHR.entry[min_index].data << dec << " num_returned: " << MSHR.num_returned;
        cout << " event: " << MSHR.entry[min_index].event_cycle << " current: " << current_core_cycle[MSHR.entry[min_index].cpu] << " next: " << MSHR.next_fill_cycle << endl; });
  }
}
