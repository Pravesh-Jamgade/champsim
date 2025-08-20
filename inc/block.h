#ifndef BLOCK_H
#define BLOCK_H

#include <algorithm>
#include <vector>

#include "champsim_constants.h"
#include "circular_buffer.hpp"
#include "instruction.h"
#include "user.h"

class MemoryRequestProducer;
class LSQ_ENTRY;


enum VF
{
  victima = 0,
  // we do peek cache line before sending victima packet, if cache line is found then we make PTW packet a dumy and victima as actual.
  // PTW packet is needed to simulate traffic, we will discard its results.
  // Its possible cacheline gets evicted while we wait in the queue, this happens after we already have decided which could be the dumy or actual packet.
  // This will create problems at return_data. TODO: add mechanism to accept results of dummy packets as well when the victima packet fails to retrive.
  victima_dumy,
  victima_acutal_packet_miss,
  recv_victima,
  valid_psc_event,

  ptw_copy,

  PACKET_DP_RECV,
  PACKET_AP_RECV,
  MSHR_WAIT_AP,
  MSHR_WAIT_DP,
  VF_END
};

enum POM
{
  POM = 0,
  POM_MISS,
  POM_To_PTW,
  POM_END
};

enum State
{
  PTW_FILL,
  PSC_Search,
  State_end
};

enum CYCLE_ENQ
{
  TS_ADD_QUEUE,
  TS_ADD_MSHR,
  CYCLE_ENQ_END
};

// message packet
class PACKET
{
public:
  bool scheduled = false;

  uint8_t asid[2] = {std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max()}, type = 0, fill_level = 0, pf_origin_level = 0;

  uint32_t pf_metadata;
  uint32_t cpu = NUM_CPUS;

  uint64_t address = 0, v_address = 0, data = 0, instr_id = 0, ip = 0, event_cycle = std::numeric_limits<uint64_t>::max(), cycle_enqueued = 0;

  std::vector<std::vector<LSQ_ENTRY>::iterator> lq_index_depend_on_me = {}, sq_index_depend_on_me = {};
  std::vector<champsim::circular_buffer<ooo_model_instr>::iterator> instr_depend_on_me;
  std::vector<MemoryRequestProducer*> to_return;

  uint8_t translation_level = 0, init_translation_level = 0;

  uint64_t uv_cycle_enqueue = 0;
  uint64_t type_cycle_enqueued[CYCLE_ENQ::CYCLE_ENQ_END] = {0};
  bool ttp = false;

  bool vflag[VF::VF_END] = {0};
  bool pomflag[POM::POM_END] = {0};

  // default: wait for Actual Packet
  VF mshr_state = VF::VF_END;
  int thread_id=-1;

  CACHE_ID hit_where = CACHE_ID::CACHE_ID_END;
  
  State state = State::State_end;

  DataType dtype = DataType::INVALID;
};

template <>
struct is_valid<PACKET> {
  bool operator()(const PACKET& test) { return test.address != 0; }
};

template <typename LIST>
void packet_dep_merge(LIST& dest, LIST& src)
{
  dest.reserve(std::size(dest) + std::size(src));
  auto middle = std::end(dest);
  dest.insert(middle, std::begin(src), std::end(src));
  std::inplace_merge(std::begin(dest), middle, std::end(dest));
  auto uniq_end = std::unique(std::begin(dest), std::end(dest));
  dest.erase(uniq_end, std::end(dest));
}

// load/store queue
struct LSQ_ENTRY {
  uint64_t instr_id = 0, producer_id = std::numeric_limits<uint64_t>::max(), virtual_address = 0, physical_address = 0, ip = 0, event_cycle = 0;

  champsim::circular_buffer<ooo_model_instr>::iterator rob_index;

  uint8_t translated = 0, fetched = 0, asid[2] = {std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max()};
};

template <>
struct is_valid<LSQ_ENTRY> {
  bool operator()(const LSQ_ENTRY& test) { return test.virtual_address != 0; }
};

#endif
