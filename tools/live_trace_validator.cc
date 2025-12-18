#include <getopt.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "instruction.h"
#include "tracereader.h"

namespace {

struct MemoryPresence {
  bool has_destination = false;
  bool has_source = false;
};

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

MemoryPresence detect_memory_fields(const ooo_model_instr& instr)
{
  MemoryPresence presence;

  for (int i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
    if (instr.destination_memory[i] != 0) {
      presence.has_destination = true;
      break;
    }
  }

  for (int i = 0; i < NUM_INSTR_SOURCES; i++) {
    if (instr.source_memory[i] != 0) {
      presence.has_source = true;
      break;
    }
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

void print_missing_examples(const std::vector<ooo_model_instr>& missing)
{
  if (missing.empty())
    return;

  std::cout << "\nFirst " << missing.size() << " instructions without memory fields:\n";
  for (std::size_t i = 0; i < missing.size(); i++) {
    const auto& instr = missing[i];
    std::cout << "  [" << i + 1 << "] ip=0x" << std::hex << instr.ip << std::dec;
    std::cout << " dest_mem=(";
    for (int dest = 0; dest < NUM_INSTR_DESTINATIONS; dest++) {
      if (dest > 0)
        std::cout << ", ";
      std::cout << instr.destination_memory[dest];
    }
    std::cout << ") src_mem=(";
    for (int src = 0; src < NUM_INSTR_SOURCES; src++) {
      if (src > 0)
        std::cout << ", ";
      std::cout << instr.source_memory[src];
    }
    std::cout << ")\n";
  }
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
  std::unique_ptr<tracereader> trace(get_tracereader<input_instr>(trace_command, 0, false, true));

  std::size_t instructions_seen = 0;
  std::size_t instructions_with_memory = 0;
  std::size_t instructions_without_memory = 0;
  std::vector<ooo_model_instr> missing_examples;

  while (limit == 0 || instructions_seen < limit) {
    const ooo_model_instr instr = trace->get();
    instructions_seen++;

    MemoryPresence presence = detect_memory_fields(instr);
    const bool has_memory = presence.has_destination || presence.has_source;

    if (has_memory) {
      instructions_with_memory++;
    } else {
      instructions_without_memory++;
      if (missing_examples.size() < 10)
        missing_examples.push_back(instr);
      if (verbose_missing) {
        std::cout << "[Missing] #" << instructions_seen << " ip=0x" << std::hex << instr.ip << std::dec << '\n';
      }
    }

    if (report_interval > 0 && instructions_seen % report_interval == 0) {
      std::cout << "[Progress] checked " << instructions_seen << " instructions (with memory: " << instructions_with_memory
                << ", without memory: " << instructions_without_memory << ")\n";
    }
  }

  std::cout << "\nFinished reading " << instructions_seen << " instructions from live trace command: \"" << trace_command << "\"\n";
  std::cout << "Instructions with memory fields:    " << instructions_with_memory << '\n';
  std::cout << "Instructions missing memory fields: " << instructions_without_memory << '\n';

  print_missing_examples(missing_examples);

  return instructions_without_memory == 0 ? 0 : 1;
}
