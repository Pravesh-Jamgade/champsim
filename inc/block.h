#ifndef BLOCK_H
#define BLOCK_H

#include <algorithm>
#include <vector>

#include "champsim_constants.h"
#include "circular_buffer.hpp"
#include "instruction.h"

#include "utils.h"

class MemoryRequestProducer;
class LSQ_ENTRY;

enum Flags
{
  TLB_Miss_Address = 0, // Address is part of Physical Page which missed in TLB hierarchy
  Page_Fault_Address, // Address is part of Physical Page which caused Page fault
  Packet_is_Part_of_Moving_Window, // To assit in counting the number of packets within a virtual kind of window. As event cycle may cause counting again and again.
  Packet_is_Counted_for_Merge,
  Packet_Flags_End
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

  uint64_t ptw_cycle_enqueue = 0;
  uint64_t translation_time = std::numeric_limits<uint64_t>::max();
  uint64_t access_time = std::numeric_limits<uint64_t>::max();
  bool ttp = false;

  CACHE_ID hit_where = CACHE_ID::CACHE_ID_END;
  bool packet_flags [Flags::Packet_Flags_End] = {false};
};

class MSHR_ENTRY: public PACKET
{
  public:
  int limit = 8;
  vector<uint64_t> sub_addresses;
  vector<uint64_t> sub_data;
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

  //usercode
  bool packet_flags [Flags::Packet_Flags_End] = {false};
};

template <>
struct is_valid<LSQ_ENTRY> {
  bool operator()(const LSQ_ENTRY& test) { return test.virtual_address != 0; }
};


#endif
