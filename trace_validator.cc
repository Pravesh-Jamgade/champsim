#include <getopt.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "inc/instruction.h"
#include "inc/tracereader.h"

namespace {

struct MemoryPresence {
  // 1 for store and 2 for load
  uint32_t is_st_or_load = 0;
  bool has_destination = false;
  bool has_source = false;

  uint64_t src_addr = 0;
  uint64_t dst_addr = 0;
};


 // Convert integer to hex string
 std::string intToHex(uint64_t value) {
  std::ostringstream oss;
  oss << std::hex << std::uppercase << value;
  return oss.str();
}


void print_usage(const char* argv0)
{
  std::cout << "Usage: " << argv0 << " [options] <live-trace-command>\n\n";
  std::cout << "Reads a live ChampSim trace using the existing tracereader interface and\n";
  std::cout << "verifies whether each instruction contains memory address information.\n\n";
  std::cout << "Options:\n";
  std::cout << "  -n, --limit <N>           Maximum number of instructions to inspect (0 = no limit)\n";
  std::cout << "  -r, --report-interval <N> Print a progress line every N instructions (0 = disable)\n";
  std::cout << "  -v, --verbose-missing     Print every instruction that is missing memory fields\n";
  std::cout << "  -h, --help                Show this message\n";
}

MemoryPresence detect_memory_fields(const ooo_model_instr& instr, std::string cmd)
{
  MemoryPresence presence;

  for (int i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
    if (instr.destination_memory[i] > 0) {
      presence.has_destination = true;
      presence.dst_addr = instr.destination_memory[i];
      break;
    }
  }

  for (int i = 0; i < NUM_INSTR_SOURCES; i++) {
    if (instr.source_memory[i] > 0) {
      presence.has_source = true;
      presence.src_addr = instr.source_memory[i];
      break;
    }
  }

  // Mismatch
  // load addr found but instr is marked store type OR store address found but instr us marked load type
  if((presence.has_source && instr.id==1))
  {
    std::cout << "cmd: " << cmd << ", Mismatch: "  << "has_source, " << presence.has_source << ", st/ld" << instr.id << ", addr, " << intToHex(presence.src_addr) << '\n'; 
    exit(0);
  }
  else if((presence.has_destination && instr.id==2))
  {
    std::cout << "cmd: " << cmd << ", Mismatch: "  << "has_source, " << presence.has_destination << ", st/ld" << instr.id << ", addr, " << intToHex(presence.dst_addr) << '\n'; 
    exit(0);
  }

  if(instr.ip < 0)
  {
    std::cout << "ip == 0 \n";
    if(presence.has_source) std::cout << "cmd: " << cmd  << ", " << presence.has_source << ", st/ld" << instr.id << ", addr, " << intToHex(presence.src_addr) << '\n'; 
    else if(presence.has_destination) std::cout << "cmd: " << cmd  << ", "<< presence.has_destination<< ", st/ld" << instr.id << ", addr, " << intToHex(presence.dst_addr) << '\n'; 
    else std::cout << "non-mem instr\n";
    exit(0);
  }
  return presence;
}

std::string join_command(int argc, char** argv, int start_index)
{
  std::ostringstream builder;
  for (int i = start_index; i < argc; i++) {
    if (i > start_index)
      builder << ' ';
    builder << argv[i];
  }

  return builder.str();
}

} // namespace

int main(int argc, char** argv)
{
  std::size_t limit = 0;
  std::size_t report_interval = 1000000;
  bool verbose_missing = false;

  const option kOptions[] = {{"limit", required_argument, nullptr, 'n'},
                             {"report-interval", required_argument, nullptr, 'r'},
                             {"verbose-missing", no_argument, nullptr, 'v'},
                             {"help", no_argument, nullptr, 'h'},
                             {nullptr, 0, nullptr, 0}};

  while (true) {
    int option_index = 0;
    int opt = getopt_long(argc, argv, "n:r:vh", kOptions, &option_index);
    if (opt == -1)
      break;

    switch (opt) {
    case 'n':
      limit = std::stoull(optarg);
      break;
    case 'r':
      report_interval = std::stoull(optarg);
      break;
    case 'v':
      verbose_missing = true;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  if (optind >= argc) {
    print_usage(argv[0]);
    return 1;
  }

  const std::string trace_command = join_command(argc, argv, optind);

  std::cout << "Trace Command: " << trace_command << '\n';
  std::unique_ptr<tracereader> trace(get_tracereader<context_instr>(trace_command, 0, false, true));

  std::size_t instructions_seen = 0;
  std::size_t instructions_with_memory = 0;
  std::size_t instructions_without_memory = 0;
  std::vector<ooo_model_instr> missing_examples;

  while (limit == 0 || instructions_seen < limit) {
    const ooo_model_instr instr = trace->get();
    instructions_seen++;
    MemoryPresence ret = detect_memory_fields(instr, trace_command);
    if(ret.has_source || ret.has_destination)
      instructions_with_memory++;
    else instructions_without_memory++;
  }

  std::cout << "CMD: " << trace_command << "total, " << instructions_seen << ", with_mem, " << instructions_with_memory << ", without_mem, " << instructions_without_memory << '\n';

  return 0;
}