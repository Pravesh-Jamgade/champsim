#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <getopt.h>
#include <iomanip>
#include <signal.h>
#include <string.h>
#include <vector>

#include "cache.h"
#include "champsim.h"
#include "champsim_constants.h"
#include "dram_controller.h"
#include "ooo_cpu.h"
#include "operable.h"
#include "tracereader.h"
#include "vmem.h"
#include "trace_instruction.h"
#include "dramsim3_wrapper.hpp"
#include "INIReader.h"
#include "victima.h"
#include "hist.h"
#include "pagetable.h"

vector<PageTable*> ptt;
map<uint64_t, PTWC> ptw_pred;
list<pair<string, uint64_t>> hash_cache;
map<uint64_t, uint64_t> l2_pte_map;

uint8_t warmup_complete[NUM_CPUS] = {}, all_warmup_complete = 0, all_simulation_complete = 0,
        MAX_INSTR_DESTINATIONS = NUM_INSTR_DESTINATIONS, knob_cloudsuite = 0, knob_low_bandwidth = 0;


vector<uint8_t> simulation_complete;
uint64_t warmup_instructions = 1000000, simulation_instructions = 10000000;

auto start_time = time(NULL);

// For backwards compatibility with older module source.
champsim::deprecated_clock_cycle current_core_cycle;

extern DRAMSim3_DRAM DRAM;
extern VirtualMemory vmem;
extern std::array<O3_CPU*, NUM_CPUS> ooo_cpu;
extern std::array<CACHE*, NUM_CACHES> caches;
extern std::array<champsim::operable*, NUM_OPERABLES> operables;
extern const char* instantiation_code;

// Extra configguration
extern int KNOB_TRANSLATION_QUEUE;
extern int KNOB_TTP;
extern int KNOB_STLB_DO_NOT_TRACK_MISS;
extern int KNOB_STTMRAM_STLB;
extern int KNOB_VICTIMA, KNOB_EXTEND_VICTIMA, KNOB_HASH_CACHE_MAX_LIMIT;
extern int KNOB_IDEAL_VICTIMA, KNOB_POMTLB;
extern int KNOB_SMT_ENABLE;
extern int KNOB_PSCL_ROOT_LEVEL;
extern int KNOB_LIVE_INPUT;
extern int KNOB_ENABLE_LOG;
extern int KNOB_ENABLE_MFOE_V2;
extern int KNOB_ENABLE_CTX;

std::vector<tracereader*> traces;

void print_ptw_freq_and_cost();

uint64_t champsim::deprecated_clock_cycle::operator[](std::size_t cpu_idx)
{
  static bool deprecate_printed = false;
  if (!deprecate_printed) {
    std::cout << "WARNING: The use of 'current_core_cycle[cpu]' is deprecated." << std::endl;
    std::cout << "WARNING: Use 'this->current_cycle' instead." << std::endl;
    deprecate_printed = true;
  }
  return ooo_cpu[cpu_idx]->current_cycle;
}

void record_roi_stats(uint32_t cpu, CACHE* cache)
{
  for (uint32_t i = 0; i < NUM_TYPES; i++) {
    cache->roi_access[cpu][i] = cache->sim_access[cpu][i];
    cache->roi_hit[cpu][i] = cache->sim_hit[cpu][i];
    cache->roi_miss[cpu][i] = cache->sim_miss[cpu][i];
  }
}

void print_roi_stats(uint32_t cpu, CACHE* cache)
{
  uint64_t TOTAL_ACCESS = 0, TOTAL_HIT = 0, TOTAL_MISS = 0;

  for (uint32_t i = 0; i < NUM_TYPES; i++) {
    TOTAL_ACCESS += cache->roi_access[cpu][i];
    TOTAL_HIT += cache->roi_hit[cpu][i];
    TOTAL_MISS += cache->roi_miss[cpu][i];
  }

  if (TOTAL_ACCESS > 0) {
    cout << cache->NAME;
    cout << " TOTAL     ACCESS: " << setw(10) << TOTAL_ACCESS << "  HIT: " << setw(10) << TOTAL_HIT << "  MISS: " << setw(10) << TOTAL_MISS << endl;

    cout << cache->NAME;
    cout << " LOAD      ACCESS: " << setw(10) << cache->roi_access[cpu][0] << "  HIT: " << setw(10) << cache->roi_hit[cpu][0] << "  MISS: " << setw(10)
         << cache->roi_miss[cpu][0] << endl;

    cout << cache->NAME;
    cout << " RFO       ACCESS: " << setw(10) << cache->roi_access[cpu][1] << "  HIT: " << setw(10) << cache->roi_hit[cpu][1] << "  MISS: " << setw(10)
         << cache->roi_miss[cpu][1] << endl;

    cout << cache->NAME;
    cout << " PREFETCH  ACCESS: " << setw(10) << cache->roi_access[cpu][2] << "  HIT: " << setw(10) << cache->roi_hit[cpu][2] << "  MISS: " << setw(10)
         << cache->roi_miss[cpu][2] << endl;

    cout << cache->NAME;
    cout << " WRITEBACK ACCESS: " << setw(10) << cache->roi_access[cpu][3] << "  HIT: " << setw(10) << cache->roi_hit[cpu][3] << "  MISS: " << setw(10)
         << cache->roi_miss[cpu][3] << endl;

    cout << cache->NAME;
    cout << " TRANSLATION ACCESS: " << setw(10) << cache->roi_access[cpu][4] << "  HIT: " << setw(10) << cache->roi_hit[cpu][4] << "  MISS: " << setw(10)
         << cache->roi_miss[cpu][4] << endl;

    cout << cache->NAME;
    cout << " PREFETCH  REQUESTED: " << setw(10) << cache->pf_requested << "  ISSUED: " << setw(10) << cache->pf_issued;
    cout << "  USEFUL: " << setw(10) << cache->pf_useful << "  USELESS: " << setw(10) << cache->pf_useless << endl;

    cout << cache->NAME;
    cout << " AVERAGE MISS LATENCY: " << (1.0 * (cache->total_miss_latency)) / TOTAL_MISS << " cycles" << endl;
    // cout << " AVERAGE MISS LATENCY: " <<
    // (cache->total_miss_latency)/TOTAL_MISS << " cycles " <<
    // cache->total_miss_latency << "/" << TOTAL_MISS<< endl;
  }
}

void print_sim_stats(uint32_t cpu, CACHE* cache)
{
  uint64_t TOTAL_ACCESS = 0, TOTAL_HIT = 0, TOTAL_MISS = 0;

  for (uint32_t i = 0; i < NUM_TYPES; i++) {
    TOTAL_ACCESS += cache->sim_access[cpu][i];
    TOTAL_HIT += cache->sim_hit[cpu][i];
    TOTAL_MISS += cache->sim_miss[cpu][i];
  }

  if (TOTAL_ACCESS > 0) {
    cout << cache->NAME;
    cout << " TOTAL     ACCESS: " << setw(10) << TOTAL_ACCESS << "  HIT: " << setw(10) << TOTAL_HIT << "  MISS: " << setw(10) << TOTAL_MISS << endl;

    cout << cache->NAME;
    cout << " LOAD      ACCESS: " << setw(10) << cache->sim_access[cpu][0] << "  HIT: " << setw(10) << cache->sim_hit[cpu][0] << "  MISS: " << setw(10)
         << cache->sim_miss[cpu][0] << endl;

    cout << cache->NAME;
    cout << " RFO       ACCESS: " << setw(10) << cache->sim_access[cpu][1] << "  HIT: " << setw(10) << cache->sim_hit[cpu][1] << "  MISS: " << setw(10)
         << cache->sim_miss[cpu][1] << endl;

    cout << cache->NAME;
    cout << " PREFETCH  ACCESS: " << setw(10) << cache->sim_access[cpu][2] << "  HIT: " << setw(10) << cache->sim_hit[cpu][2] << "  MISS: " << setw(10)
         << cache->sim_miss[cpu][2] << endl;

    cout << cache->NAME;
    cout << " WRITEBACK ACCESS: " << setw(10) << cache->sim_access[cpu][3] << "  HIT: " << setw(10) << cache->sim_hit[cpu][3] << "  MISS: " << setw(10)
         << cache->sim_miss[cpu][3] << endl;
  }
}

void print_branch_stats()
{
  for (uint32_t i = 0; i < NUM_CPUS; i++) {
    cout << endl << "CPU " << i << " Branch Prediction Accuracy: ";
    cout << (100.0 * (ooo_cpu[i]->num_branch - ooo_cpu[i]->branch_mispredictions)) / ooo_cpu[i]->num_branch;
    for(size_t th = 0; th< KNOB_SMT_ENABLE; th++)
    {
      cout << "% MPKI: " << (1000.0 * ooo_cpu[i]->branch_mispredictions) / (ooo_cpu[i]->num_retired[th] - warmup_instructions);
      cout << " Average ROB Occupancy at Mispredict: " << (1.0 * ooo_cpu[i]->total_rob_occupancy_at_branch_mispredict) / ooo_cpu[i]->branch_mispredictions
         << endl;
      cout << "Branch type MPKI" << endl;
      cout << "BRANCH_DIRECT_JUMP: " << (1000.0 * ooo_cpu[i]->branch_type_misses[1] / (ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr)) << endl;
      cout << "BRANCH_INDIRECT: " << (1000.0 * ooo_cpu[i]->branch_type_misses[2] / (ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr)) << endl;
      cout << "BRANCH_CONDITIONAL: " << (1000.0 * ooo_cpu[i]->branch_type_misses[3] / (ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr)) << endl;
      cout << "BRANCH_DIRECT_CALL: " << (1000.0 * ooo_cpu[i]->branch_type_misses[4] / (ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr)) << endl;
      cout << "BRANCH_INDIRECT_CALL: " << (1000.0 * ooo_cpu[i]->branch_type_misses[5] / (ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr)) << endl;
      cout << "BRANCH_RETURN: " << (1000.0 * ooo_cpu[i]->branch_type_misses[6] / (ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr)) << endl << endl;
    }
    

    /*
    cout << "Branch types" << endl;
    cout << "NOT_BRANCH: " << ooo_cpu[i]->total_branch_types[0] << " " <<
    (100.0*ooo_cpu[i]->total_branch_types[0])/(ooo_cpu[i]->num_retired -
    ooo_cpu[i]->begin_sim_instr) << "%" << endl; cout << "BRANCH_DIRECT_JUMP: "
    << ooo_cpu[i]->total_branch_types[1] << " " <<
    (100.0*ooo_cpu[i]->total_branch_types[1])/(ooo_cpu[i]->num_retired -
    ooo_cpu[i]->begin_sim_instr) << "%" << endl; cout << "BRANCH_INDIRECT: " <<
    ooo_cpu[i]->total_branch_types[2] << " " <<
    (100.0*ooo_cpu[i]->total_branch_types[2])/(ooo_cpu[i]->num_retired -
    ooo_cpu[i]->begin_sim_instr) << "%" << endl; cout << "BRANCH_CONDITIONAL: "
    << ooo_cpu[i]->total_branch_types[3] << " " <<
    (100.0*ooo_cpu[i]->total_branch_types[3])/(ooo_cpu[i]->num_retired -
    ooo_cpu[i]->begin_sim_instr) << "%" << endl; cout << "BRANCH_DIRECT_CALL: "
    << ooo_cpu[i]->total_branch_types[4] << " " <<
    (100.0*ooo_cpu[i]->total_branch_types[4])/(ooo_cpu[i]->num_retired -
    ooo_cpu[i]->begin_sim_instr) << "%" << endl; cout << "BRANCH_INDIRECT_CALL:
    " << ooo_cpu[i]->total_branch_types[5] << " " <<
    (100.0*ooo_cpu[i]->total_branch_types[5])/(ooo_cpu[i]->num_retired -
    ooo_cpu[i]->begin_sim_instr) << "%" << endl; cout << "BRANCH_RETURN: " <<
    ooo_cpu[i]->total_branch_types[6] << " " <<
    (100.0*ooo_cpu[i]->total_branch_types[6])/(ooo_cpu[i]->num_retired -
    ooo_cpu[i]->begin_sim_instr) << "%" << endl; cout << "BRANCH_OTHER: " <<
    ooo_cpu[i]->total_branch_types[7] << " " <<
    (100.0*ooo_cpu[i]->total_branch_types[7])/(ooo_cpu[i]->num_retired -
    ooo_cpu[i]->begin_sim_instr) << "%" << endl << endl;
    */
  }
}

// void print_dram_stats()
// {
//   uint64_t total_congested_cycle = 0;
//   uint64_t total_congested_count = 0;

//   std::cout << std::endl;
//   std::cout << "DRAM Statistics" << std::endl;
//   for (uint32_t i = 0; i < DRAM_CHANNELS; i++) {
//     std::cout << " CHANNEL " << i << std::endl;

//     auto& channel = DRAM.channels[i];
//     std::cout << " RQ ROW_BUFFER_HIT: " << std::setw(10) << channel.RQ_ROW_BUFFER_HIT << " ";
//     std::cout << " ROW_BUFFER_MISS: " << std::setw(10) << channel.RQ_ROW_BUFFER_MISS;
//     std::cout << std::endl;

//     std::cout << " DBUS AVG_CONGESTED_CYCLE: ";
//     if (channel.dbus_count_congested)
//       std::cout << std::setw(10) << ((double)channel.dbus_cycle_congested / channel.dbus_count_congested);
//     else
//       std::cout << "-";
//     std::cout << std::endl;

//     std::cout << " WQ ROW_BUFFER_HIT: " << std::setw(10) << channel.WQ_ROW_BUFFER_HIT << " ";
//     std::cout << " ROW_BUFFER_MISS: " << std::setw(10) << channel.WQ_ROW_BUFFER_MISS << " ";
//     std::cout << " FULL: " << std::setw(10) << channel.WQ_FULL;
//     std::cout << std::endl;

//     std::cout << std::endl;

//     total_congested_cycle += channel.dbus_cycle_congested;
//     total_congested_count += channel.dbus_count_congested;
//   }

//   if (DRAM_CHANNELS > 1) {
//     std::cout << " DBUS AVG_CONGESTED_CYCLE: ";
//     if (total_congested_count)
//       std::cout << std::setw(10) << ((double)total_congested_cycle / total_congested_count);
//     else
//       std::cout << "-";

//     std::cout << std::endl;
//   }
// }

void reset_cache_stats(uint32_t cpu, CACHE* cache)
{
  for (uint32_t i = 0; i < NUM_TYPES; i++) {
    cache->sim_access[cpu][i] = 0;
    cache->sim_hit[cpu][i] = 0;
    cache->sim_miss[cpu][i] = 0;
  }

  cache->pf_requested = 0;
  cache->pf_issued = 0;
  cache->pf_useful = 0;
  cache->pf_useless = 0;
  cache->pf_fill = 0;

  cache->total_miss_latency = 0;

  cache->RQ_ACCESS = 0;
  cache->RQ_MERGED = 0;
  cache->RQ_TO_CACHE = 0;

  cache->WQ_ACCESS = 0;
  cache->WQ_MERGED = 0;
  cache->WQ_TO_CACHE = 0;
  cache->WQ_FORWARD = 0;
  cache->WQ_FULL = 0;

  cache->reset_datamodel();
}

void finish_warmup()
{
  uint64_t elapsed_second = (uint64_t)(time(NULL) - start_time), elapsed_minute = elapsed_second / 60, elapsed_hour = elapsed_minute / 60;
  elapsed_minute -= elapsed_hour * 60;
  elapsed_second -= (elapsed_hour * 3600 + elapsed_minute * 60);

  // reset core latency
  // note: since re-ordering he function calls in the main simulation loop, it's
  // no longer necessary to add
  //       extra latency for scheduling and execution, unless you want these
  //       steps to take longer than 1 cycle.
  // PAGE_TABLE_LATENCY = 100;
  // SWAP_LATENCY = 100000;

  cout << endl;
  for (uint32_t i = 0; i < NUM_CPUS; i++) 
  {

    for(size_t th =0; th < KNOB_SMT_ENABLE; th++)
    {
      cout << "Warmup complete CPU " << i << " instructions: " << ooo_cpu[i]->num_retired[th] << " cycles: " << ooo_cpu[i]->current_cycle;
      cout << " (Simulation time: " << elapsed_hour << " hr " << elapsed_minute << " min " << elapsed_second << " sec) " << endl;

      ooo_cpu[i]->begin_sim_cycle = ooo_cpu[i]->current_cycle;
      ooo_cpu[i]->begin_sim_instr = ooo_cpu[i]->num_retired[th];
    }
    
    // reset branch stats
    ooo_cpu[i]->num_branch = 0;
    ooo_cpu[i]->branch_mispredictions = 0;
    ooo_cpu[i]->total_rob_occupancy_at_branch_mispredict = 0;

    for (uint32_t j = 0; j < 8; j++) {
      ooo_cpu[i]->total_branch_types[j] = 0;
      ooo_cpu[i]->branch_type_misses[j] = 0;
    }

    for (auto it = caches.rbegin(); it != caches.rend(); ++it)
      reset_cache_stats(i, *it);
  }
  cout << endl;

  // // reset DRAM stats
  // for (uint32_t i = 0; i < DRAM_CHANNELS; i++) {
  //   DRAM.channels[i].WQ_ROW_BUFFER_HIT = 0;
  //   DRAM.channels[i].WQ_ROW_BUFFER_MISS = 0;
  //   DRAM.channels[i].RQ_ROW_BUFFER_HIT = 0;
  //   DRAM.channels[i].RQ_ROW_BUFFER_MISS = 0;
  // }
}

CACHE* get_cache_by_name(string cacheName)
{
  for(auto cache: caches)
  {
    if(cache->NAME.find(cacheName)!=string::npos)
      return cache;
  }
  return nullptr;
}

void overwrite_cache()
{
  // setting stlb as STT-mram
  if(KNOB_STTMRAM_STLB)
  {
    CACHE* stlb = get_cache_by_name("STLB");
    stlb->WRITE_LANTENCY = 3 * stlb->HIT_LATENCY;
    stlb->FILL_LATENCY = 3 * stlb->HIT_LATENCY;
  }
  
  if(KNOB_VICTIMA || KNOB_POMTLB)
  {
    CACHE* stlb = get_cache_by_name("STLB");

    CACHE* l2 = get_cache_by_name("L2");
    stlb->l2cache = l2;

    CACHE* l1 = get_cache_by_name("L1D");
    stlb->l1cache = l1;
  }

  for(auto op: operables)
  {
    if(op->NAME.find("PTW") != string::npos)
    {
      op->_overwrite();
    }
  }
}

void signal_handler(int signal)
{
  cout << "Caught signal: " << signal << endl;

  for(int i=0; i< NUM_CPUS; i++)
  {
    // simulation complete
    for(int j=0; j< KNOB_SMT_ENABLE; j++)
    {
      // summation across threads
      uint64_t finish_sim_instr = ooo_cpu[i]->num_retired[j] - ooo_cpu[i]->begin_sim_instr;
      uint64_t finish_sim_cycle = ooo_cpu[i]->current_cycle - ooo_cpu[i]->begin_sim_cycle;

      cout << "Stats CPU " << i << " Thread " << j << " instructions: " << finish_sim_instr << " cycles: " <<finish_sim_cycle << '\n';
      // cout << " cumulative IPC: " << ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle);
      // cout << " (Simulation time: " << elapsed_hour << " hr " << elapsed_minute << " min " << elapsed_second << " sec) " << endl;
      // cout << "cpu" << i << " IPC, " << ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle) << '\n';

      uint64_t elapsed_second = (uint64_t)(time(NULL) - start_time), elapsed_minute = elapsed_second / 60, elapsed_hour = elapsed_minute / 60;
      elapsed_minute -= elapsed_hour * 60;
      elapsed_second -= (elapsed_hour * 3600 + elapsed_minute * 60);

      cout << "Stats cpu" << i << " simtime, " << elapsed_hour << ":" << elapsed_minute << ":" << elapsed_minute << '\n';
      cout << "Stats cpu" << i << ", thread" << j << ", ipc, " << ((double)finish_sim_instr/finish_sim_cycle) << '\n'; 
    }
  }

  cout << endl << "Region of Interest Statistics" << endl;
  for (uint32_t i = 0; i < NUM_CPUS; i++) 
  {
    cout << endl << "IPC " << i << ", " <<  ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle) << '\n';
  }

  for (uint32_t i = 0; i < NUM_CPUS; i++) 
  {
    cout << endl << "CPU " << i << " cumulative IPC: " << ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle);
    cout << " instructions: " << ooo_cpu[i]->finish_sim_instr << " cycles: " << ooo_cpu[i]->finish_sim_cycle << endl;

    for (auto it = caches.rbegin(); it != caches.rend(); ++it)
      print_roi_stats(i, *it);
  }

  
  for (auto it = caches.rbegin(); it != caches.rend(); ++it)
    (*it)->impl_prefetcher_final_stats();

  for (auto it = caches.rbegin(); it != caches.rend(); ++it)
    (*it)->impl_replacement_final_stats();


  exit(1);
}

int main(int argc, char** argv)
{
  // interrupt signal hanlder
  struct sigaction sigIntHandler;
  sigIntHandler.sa_handler = signal_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;
  sigaction(SIGINT, &sigIntHandler, NULL);

  
  // initialize knobs
  uint8_t show_heartbeat = 1;
  uint32_t context_switch_counter = 0;

  // check to see if knobs changed using getopt_long()
  int traces_encountered = 0;
  static struct option long_options[] = {{"warmup_instructions", required_argument, 0, 'w'},
                                         {"simulation_instructions", required_argument, 0, 'i'},
                                         {"output", required_argument, 0, 'o'},
                                         {"config", no_argument, 0, 'x'},
                                         {"hide_heartbeat", no_argument, 0, 'h'},
                                         {"cloudsuite", no_argument, 0, 'c'},
                                         {"traces", no_argument, &traces_encountered, 1},
                                         {0, 0, 0, 0}};

  string configini_path = string("./config.ini");
  string output_file = "default";
  string trace_shared_buff = "";
  int c;
  while ((c = getopt_long_only(argc, argv, "w:i:o:x:hc", long_options, NULL)) != -1 && !traces_encountered) {
    switch (c) {
    case 'w':
      warmup_instructions = atol(optarg);
      break;
    case 'i':
      simulation_instructions = atol(optarg);
      break;
    case 'h':
      show_heartbeat = 0;
      break;
    case 'o':
      output_file = string(optarg);
      break;
    case 'x':
      configini_path = string(optarg);
      break;
    case 'c':
      knob_cloudsuite = 1;
      MAX_INSTR_DESTINATIONS = NUM_INSTR_DESTINATIONS_SPARC;
      break;
    case 0:
      break;
    default:
      abort();
    }
  }

  trace_shared_buff += "/tmp/"+ output_file;
  output_file += ".log";
  // std::ofstream out(output_file.c_str());
  // std::streambuf *coutbuf = std::cout.rdbuf(); //save old buf
  // std::cout.rdbuf(out.rdbuf()); //redirect std::cout to out.txt!
  freopen(output_file.c_str(),"w",stdout);

  cout << endl << "*** ChampSim Multicore Out-of-Order Simulator ***" << endl << endl;
  cout << "Config Path: " << configini_path << '\n';
  cout << "Warmup Instructions: " << warmup_instructions << endl;
  cout << "Simulation Instructions: " << simulation_instructions << endl;
  cout << "Number of CPUs: " << NUM_CPUS << endl;

  long long int dram_size = DRAM_CHANNELS * DRAM_RANKS * DRAM_BANKS * DRAM_ROWS * DRAM_COLUMNS * BLOCK_SIZE / 1024 / 1024; // in MiB
  std::cout << "Off-chip DRAM Size: ";
  if (dram_size > 1024)
    std::cout << dram_size / 1024 << " GiB";
  else
    std::cout << dram_size << " MiB";
  std::cout << " Channels: " << DRAM_CHANNELS << " Width: " << 8 * DRAM_CHANNEL_WIDTH << "-bit Data Rate: " << DRAM_IO_FREQ << " MT/s" << std::endl;

  std::cout << std::endl;
  std::cout << "VirtualMemory physical capacity: " << std::size(vmem.ppage_free_list) * vmem.page_size;
  std::cout << " num_ppages: " << std::size(vmem.ppage_free_list) << std::endl;
  std::cout << "VirtualMemory page size: " << PAGE_SIZE << " log2_page_size: " << LOG2_PAGE_SIZE << std::endl;

  std::cout << std::endl;

  INIReader* iniReader = new INIReader(configini_path);

  KNOB_LIVE_INPUT = iniReader->GetInteger("SIMULATOR", "ENABLE_LIVE_INPUT", 0);
  KNOB_TRANSLATION_QUEUE = iniReader->GetInteger("KNOB", "TQ", 0);
  KNOB_TTP = iniReader->GetInteger("KNOB", "TTP", 0);
  KNOB_STLB_DO_NOT_TRACK_MISS = iniReader->GetInteger("KNOB", "STLB_DO_NOT_TRACK_MISS", 0);
  KNOB_STTMRAM_STLB = iniReader->GetInteger("STTMRAM", "STLB", 0);
  KNOB_VICTIMA = iniReader->GetInteger("VICTIMA", "ENABLE_VICTIMA", 0);
  KNOB_EXTEND_VICTIMA = iniReader->GetInteger("VICTIMA", "EXTEND_VICTIMA", 0);
  KNOB_HASH_CACHE_MAX_LIMIT = iniReader->GetInteger("VICTIMA", "HASH_CACHE_MAX_LIMIT", 64);
  KNOB_SMT_ENABLE = iniReader->GetInteger("SMT", "ENABLE_SMT", 0);
  

  KNOB_IDEAL_VICTIMA = iniReader->GetInteger("VICTIMA", "ENABLE_IDEAL_VICTIMA", 0);
  KNOB_SMT_ENABLE = iniReader->GetInteger("SMT", "ENABLE_SMT", 0);
  KNOB_PSCL_ROOT_LEVEL = iniReader->GetInteger("PageTable", "ROOT_PT_LEVEL", 4);
  KNOB_ENABLE_LOG = iniReader->GetInteger("SIMULATOR", "ENABLE_LOG", 0);
  KNOB_ENABLE_MFOE_V2 = iniReader->GetInteger("MFOEv2", "ENABLE_MFOE_V2", 0);
  KNOB_ENABLE_CTX = iniReader->GetInteger("SIMULATOR", "ENABLE_CTX_SWITCH", 0);
  KNOB_POMTLB = iniReader->GetInteger("POMTLB", "ENABLE_POMTLB", 0);
  
  std::cout << "Extra settings:\n";
  // std::cout << "TQ="<<KNOB_TRANSLATION_QUEUE<<'\n';
  // std::cout << "TTP="<<KNOB_TTP<<'\n';
  // std::cout << "STLB_DO_NOT_TRACK_MISS="<<KNOB_STLB_DO_NOT_TRACK_MISS<<'\n';
  // std::cout << "STTMRAM_STLB="<<KNOB_STTMRAM_STLB<<'\n';
  // std::cout << "VICTIMA\n-ENABLE_VICTIMA="<<KNOB_VICTIMA<<'\n';
  // std::cout << "-ENABLE_IDEAL_VICTIMA="<<KNOB_IDEAL_VICTIMA<<'\n';
  // std::cout << "ENABLE MFOEv2=" << KNOB_ENABLE_MFOE_V2 << '\n'; 
  // std::cout << "SMT="<<KNOB_SMT_ENABLE<<'\n';
  // std::cout << "PT Levels="<<KNOB_PSCL_ROOT_LEVEL<<'\n';
  // std::cout << "Live Input="<<KNOB_LIVE_INPUT<<'\n';
  // std::cout << "Debug Log="<<KNOB_ENABLE_LOG<<'\n';
  // std::cout << "Output file="<<output_file<<'\n';
  // std::cout << "Enable Context Switch="<<KNOB_ENABLE_CTX<<'\n';
  // std::cout << '\n';

  iniReader->print();
  
  int total_cores = KNOB_SMT_ENABLE >0 ? NUM_CPUS * KNOB_SMT_ENABLE:  NUM_CPUS;
  simulation_complete.resize(total_cores, 0);

  if(KNOB_LIVE_INPUT)
  {
    traces.push_back(get_tracereader<input_instr>(trace_shared_buff, traces.size(), knob_cloudsuite, true));
    cout << "[Log]Trace Reading, " << trace_shared_buff << '\n';
    // reading memoryhog trace
    // one trace already read via live input from pintool, hence KNOB_SMT_ENABLE-1
    int get_trace_index = optind;
    for(int trace_id=1; trace_id < KNOB_SMT_ENABLE; trace_id++)
    {
      traces.push_back(get_tracereader<input_instr>(argv[get_trace_index], traces.size(), knob_cloudsuite));
      cout << "[Log]Trace Reading, " << argv[get_trace_index] << '\n';

      get_trace_index++;
    }
  }
  else
  {
    for (int i = optind; i < argc; i++) 
    {
      cout << "[Log]Trace Reading, " << argv[i] << '\n';
      traces.push_back(get_tracereader<input_instr>(argv[i], traces.size(), knob_cloudsuite));
      if(KNOB_SMT_ENABLE>0)
      {
        if(traces.size() > total_cores)
        {
          cout << "Missmatch!!!\n";
          cout << "Number of traces, " << traces.size() << '\n';
          cout << "Number of cores, " << total_cores << '\n';
          assert(0);
        }
      }
      else if (traces.size() > NUM_CPUS) {
        printf("\n*** Too many traces for the configured number of cores ***\n\n");
        assert(0);
      }
    }
  }
  

  if (traces.size() != total_cores) {
    printf("\n*** Not enough traces for the configured number of cores ***\n\n");
    assert(0);
  }

  // overwrite relevant to extra settings
  overwrite_cache();
  for(int i=0; i< 16; i++)
    ptt.push_back(new PageTable());

  printf("Simulator Configuration\n%s", instantiation_code);

  std::cout << "Cache configuration\n";
  for(auto ca: caches)
  {
    cout << ca->NAME << ", FILL Latency=" << ca->FILL_LATENCY << ", WRITE Latency=" << ca->WRITE_LANTENCY << ", HIT Latency=" << ca->HIT_LATENCY << '\n';
  }
  cout << "STLB set, " << get_cache_by_name("STLB")->NUM_SET << '\n';
  cout << '\n';

  // end trace file setup

  
  // SHARED CACHE
  for (O3_CPU* cpu : ooo_cpu) {
    cpu->o3_setup();
    cpu->initialize_core();
  }

  for (auto it = caches.rbegin(); it != caches.rend(); ++it) {
    (*it)->impl_prefetcher_initialize();
    (*it)->impl_replacement_initialize();
  }

  // simulation entry point
  while (std::any_of(std::begin(simulation_complete), std::end(simulation_complete), std::logical_not<uint8_t>())) {

    uint64_t elapsed_second = (uint64_t)(time(NULL) - start_time), elapsed_minute = elapsed_second / 60, elapsed_hour = elapsed_minute / 60;
    elapsed_minute -= elapsed_hour * 60;
    elapsed_second -= (elapsed_hour * 3600 + elapsed_minute * 60);

    for (auto op : operables) {
      try {
        op->_operate();
      } catch (champsim::deadlock& dl) {
        // ooo_cpu[dl.which]->print_deadlock();
        // std::cout << std::endl;
        // for (auto c : caches)
        for (auto c : operables) {
          c->print_deadlock();
          std::cout << std::endl;
        }

        abort();
      }
    }
    std::sort(std::begin(operables), std::end(operables), champsim::by_next_operate());

    map<uint64_t, pair<int,int>> choice_of_th;
    for(std::size_t i = 0; i < ooo_cpu.size(); ++i)
    {
      for(std::size_t j = 0; j < KNOB_SMT_ENABLE; ++j)
      {
        auto coreth = make_pair<int,int>(i,j);
        choice_of_th.insert( {ooo_cpu[i]->num_retired[j], coreth} );
      }
    }

    // for (std::size_t i = 0; i < ooo_cpu.size(); ++i) 
    for(auto entry: choice_of_th)
    {
      int i = entry.second.first;
      int th = entry.second.second;

      // for(int th=0; th< KNOB_SMT_ENABLE; th++)
      // {
        int global_th_index = i * KNOB_SMT_ENABLE + th;

        int useful_bw = ooo_cpu[i]->instrs_to_read_this_cycle;
        if(useful_bw / KNOB_SMT_ENABLE == 0)
        {
          useful_bw = useful_bw % KNOB_SMT_ENABLE;
        }
        else
        {
          useful_bw = useful_bw / KNOB_SMT_ENABLE;
        }

        // read from trace
        while (ooo_cpu[i]->fetch_stall == 0 && useful_bw > 0) {
          ooo_cpu[i]->init_instruction(traces[th]->get(), th);
          // break;
          useful_bw--;
        }

        if(KNOB_ENABLE_CTX && ooo_cpu[i]->num_retired[th] >= ooo_cpu[i]->next_ctx_instruction)
        {
          ooo_cpu[i]->next_ctx_instruction += 1000;
          context_switch_counter++;

          for(auto obj: operables)
          {
            obj->_context_switch(th);
          }
        }
        
        // heartbeat information
        if (show_heartbeat && (ooo_cpu[i]->num_retired[th] >= ooo_cpu[i]->next_print_instruction)) {
          float cumulative_ipc;
          if (warmup_complete[i])
            cumulative_ipc = (1.0 * (ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr)) / (ooo_cpu[i]->current_cycle - ooo_cpu[i]->begin_sim_cycle);
          else
            cumulative_ipc = (1.0 * ooo_cpu[i]->num_retired[th]) / ooo_cpu[i]->current_cycle;
          float heartbeat_ipc = (1.0 * ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->last_sim_instr) / (ooo_cpu[i]->current_cycle - ooo_cpu[i]->last_sim_cycle);

          cout << "Heartbeat CPU " << i << " Thread: " << th << " instructions: " << ooo_cpu[i]->num_retired[th] << " cycles: " << ooo_cpu[i]->current_cycle;
          cout << " heartbeat IPC: " << heartbeat_ipc << " cumulative IPC: " << cumulative_ipc;
          cout << " (Simulation time: " << elapsed_hour << " hr " << elapsed_minute << " min " << elapsed_second << " sec) " << endl;
          ooo_cpu[i]->next_print_instruction += STAT_PRINTING_PERIOD;
          
          ooo_cpu[i]->last_sim_instr = ooo_cpu[i]->num_retired[th];
          ooo_cpu[i]->last_sim_cycle = ooo_cpu[i]->current_cycle;
        }

        // check for warmup
        // warmup complete
        if ((warmup_complete[i] == 0) && (ooo_cpu[i]->num_retired[th] > warmup_instructions)) {
          warmup_complete[i] = 1;
          all_warmup_complete++;
        }
        if (all_warmup_complete == NUM_CPUS) { // this part is called only once
                                               // when all cores are warmed up
          all_warmup_complete++;
          finish_warmup();
        }

        // simulation complete
        if ((all_warmup_complete > NUM_CPUS) && (simulation_complete[global_th_index] == 0)
            && (ooo_cpu[i]->num_retired[th] >= (ooo_cpu[i]->begin_sim_instr + simulation_instructions))) {
          simulation_complete[global_th_index] = 1;

          // summation across threads
          ooo_cpu[i]->finish_sim_instr += ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr;
          ooo_cpu[i]->finish_sim_cycle += ooo_cpu[i]->current_cycle - ooo_cpu[i]->begin_sim_cycle;

          cout << "Finished CPU " << i << " @ Thread " << th << " instructions: " << ooo_cpu[i]->finish_sim_instr << " cycles: " << ooo_cpu[i]->finish_sim_cycle << '\n';
          // cout << " cumulative IPC: " << ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle);
          // cout << " (Simulation time: " << elapsed_hour << " hr " << elapsed_minute << " min " << elapsed_second << " sec) " << endl;
          // cout << "cpu" << i << " IPC, " << ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle) << '\n';
          
          // uint64_t elapsed_second = (uint64_t)(time(NULL) - start_time), elapsed_minute = elapsed_second / 60, elapsed_hour = elapsed_minute / 60;
          // elapsed_minute -= elapsed_hour * 60;
          // elapsed_second -= (elapsed_hour * 3600 + elapsed_minute * 60);
          
          // cout << "cpu" << i << " simtime, " << elapsed_hour << ":" << elapsed_minute << ":" << elapsed_minute << '\n';
          for (auto it = caches.rbegin(); it != caches.rend(); ++it)
            record_roi_stats(i, *it);
        }
      // }
    }
  }

  for(int i=0; i< NUM_CPUS; i++)
  {
    // simulation complete
    for(int j=0; j< KNOB_SMT_ENABLE; j++)
    {
      // summation across threads
      uint64_t finish_sim_instr = ooo_cpu[i]->num_retired[j] - ooo_cpu[i]->begin_sim_instr;
      uint64_t finish_sim_cycle = ooo_cpu[i]->current_cycle - ooo_cpu[i]->begin_sim_cycle;

      cout << "Stats CPU " << i << " Thread " << j << " instructions: " << finish_sim_instr << " cycles: " <<finish_sim_cycle << '\n';
      // cout << " cumulative IPC: " << ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle);
      // cout << " (Simulation time: " << elapsed_hour << " hr " << elapsed_minute << " min " << elapsed_second << " sec) " << endl;
      // cout << "cpu" << i << " IPC, " << ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle) << '\n';

      uint64_t elapsed_second = (uint64_t)(time(NULL) - start_time), elapsed_minute = elapsed_second / 60, elapsed_hour = elapsed_minute / 60;
      elapsed_minute -= elapsed_hour * 60;
      elapsed_second -= (elapsed_hour * 3600 + elapsed_minute * 60);

      cout << "Stats cpu" << i << " simtime, " << elapsed_hour << ":" << elapsed_minute << ":" << elapsed_minute << '\n';
    }
  }
  cout << "Stats overall context switch, " << context_switch_counter << '\n';

  cout << endl << "ChampSim completed all CPUs" << endl;
  if (NUM_CPUS > 1) {
    cout << endl << "Total Simulation Statistics (not including warmup)" << endl;
    for (uint32_t i = 0; i < NUM_CPUS; i++) {
      for(size_t th=0; th < KNOB_SMT_ENABLE; th++)
      {
        cout << endl
           << "CPU " << i
           << " cumulative IPC: " << (float)(ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr) / (ooo_cpu[i]->current_cycle - ooo_cpu[i]->begin_sim_cycle);
        
        cout << " instructions: " << ooo_cpu[i]->num_retired[th] - ooo_cpu[i]->begin_sim_instr
             << " cycles: " << ooo_cpu[i]->current_cycle - ooo_cpu[i]->begin_sim_cycle << endl;
      }
      
      for (auto it = caches.rbegin(); it != caches.rend(); ++it)
        print_sim_stats(i, *it);
    }
  }

  cout << endl << "Region of Interest Statistics" << endl;
  for (uint32_t i = 0; i < NUM_CPUS; i++) 
  {
    cout << endl << "IPC " << i << ", " <<  ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle) << '\n';
  }

  for (uint32_t i = 0; i < NUM_CPUS; i++) 
  {
    cout << endl << "CPU " << i << " cumulative IPC: " << ((float)ooo_cpu[i]->finish_sim_instr / ooo_cpu[i]->finish_sim_cycle);
    cout << " instructions: " << ooo_cpu[i]->finish_sim_instr << " cycles: " << ooo_cpu[i]->finish_sim_cycle << endl;

    for (auto it = caches.rbegin(); it != caches.rend(); ++it)
      print_roi_stats(i, *it);
  }

  
  for (auto it = caches.rbegin(); it != caches.rend(); ++it)
    (*it)->impl_prefetcher_final_stats();

  for (auto it = caches.rbegin(); it != caches.rend(); ++it)
    (*it)->impl_replacement_final_stats();

#ifndef CRC2_COMPILE
  // print_dram_stats();
  print_branch_stats();
#endif

DRAM.PrintStats();

for(auto cache: caches)
{
  cache->print_logs();
}

print_ptw_freq_and_cost();

for(int i=0; i< KNOB_SMT_ENABLE*NUM_CPUS; i++)
    ptt[i]->print_stat(i);

cout << '\n';
vmem.print_stat();
cout << "\nDone!\n";

  return 0;
}


void print_ptw_freq_and_cost()
{
  cout << '\n';
  cout << "A page has done PTW x-times (x-axis) and it has reached to DRAM y-times (y-axis) (out of x): [x][y] = event_count \n";
  cout << "PTW frequency VS cost\n";
  vector<vector<int>> ptw_freq_cost(10, vector<int>(10, 0));
  for(auto entry: ptw_pred)
  {
    int freq = entry.second.freq;
    int cost = entry.second.cost;

    int modfreq = freq % ptw_freq_cost.size();
    int modcost = cost % ptw_freq_cost[0].size();
    if(modfreq == freq && modcost == cost)
    {
      ptw_freq_cost[freq][cost]++;
    }
    else if(modfreq == freq && modcost != cost)
    {
      ptw_freq_cost[freq][9]++;
    }
    else if(modfreq != freq && modcost == cost)
    {
      ptw_freq_cost[9][cost]++;
    }
    else
    {
      ptw_freq_cost[9][9]++;
    }
  }

  // Print column headers
  cout << setw(6) << " " << "|";
  for (int i = 0; i < ptw_freq_cost[0].size(); ++i) {
      cout << setw(4) << i;
  }
  cout << '\n';

  // Print separator line
  cout << string(6, '-') << "+";
  for (int i = 0; i < ptw_freq_cost[0].size(); ++i) {
      cout << string(4, '-');
  }
  cout << '\n';

  for(int i=0; i< ptw_freq_cost.size(); i++)
  {
    cout << setw(6) << i << "|";
    for(int j=0; j< ptw_freq_cost[0].size(); j++)
    {
      cout<<setw(4)<<ptw_freq_cost[i][j];
    }
    cout<<'\n';
  }
}