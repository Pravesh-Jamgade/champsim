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
  std::unique_ptr<tracereader> trace(get_tracereader(trace_command, 0, false, true));

  std::size_t instructions_seen = 0;
  std::size_t invalid_instr = 0;
  std::size_t mem_instr = 0;
  

  while (limit == 0 || instructions_seen < limit) {
    const ooo_model_instr instr = trace->get();
    instructions_seen++;
    if(instr.ip == 0)
    {
      invalid_instr++;
      std::cout << "Exit: " << trace_command << "total, " << instructions_seen << ", invalid, " << invalid_instr << ", mem_instr, " << mem_instr << '\n';
      
      exit(0);
    }

    bool is_mem = 0;
    for(int i=0; i< NUM_INSTR_DESTINATIONS; i++)
    {
      if(instr.destination_memory[i] > 0)
      {
        is_mem = 1;
      }
    }

    for(int i=0; i< NUM_INSTR_SOURCES; i++)
    {
      if(instr.source_memory[i] > 0)
      {
        is_mem = 1;
      }
    }

    if(is_mem) mem_instr++;
  }

  std::cout << "CMD: " << trace_command << "total, " << instructions_seen << ", invalid, " << invalid_instr << ", mem_instr, " << mem_instr << '\n';

  return 0;
}