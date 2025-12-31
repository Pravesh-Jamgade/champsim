#ifndef TRACE_INSTRUCTION_H
#define TRACE_INSTRUCTION_H

#include <limits>
#define MAGIC 0x544C425452414345ULL

// instruction format
const int NUM_INSTR_DESTINATIONS_SPARC = 4;
const int NUM_INSTR_DESTINATIONS = 2;
const int NUM_INSTR_SOURCES = 4;

class LSQ_ENTRY;

// uint32_t id = 0;// 1 for store , 2 for load
struct context_instr {

  uint64_t destination_memory[NUM_INSTR_DESTINATIONS] = {};
  uint64_t source_memory[NUM_INSTR_SOURCES] = {};
  uint64_t ip = 0;
  uint64_t magic = 0;
  uint32_t record_size = 0;// 1 for store , 2 for load
  uint8_t wait = 0;
  uint8_t is_branch = 0;
  uint8_t branch_taken = 0;
  uint8_t destination_registers[NUM_INSTR_DESTINATIONS] = {};
  uint8_t source_registers[NUM_INSTR_SOURCES] = {};
};

// struct input_instr {
//   // instruction pointer or PC (Program Counter)
//   uint64_t ip = 0;

//   // branch info
//   unsigned char is_branch = 0;
//   unsigned char branch_taken = 0;

//   unsigned char destination_registers[NUM_INSTR_DESTINATIONS] = {}; // output registers
//   unsigned char source_registers[NUM_INSTR_SOURCES] = {};           // input registers

//   uint64_t destination_memory[NUM_INSTR_DESTINATIONS] = {}; // output memory
//   uint64_t source_memory[NUM_INSTR_SOURCES] = {};           // input memory
// };

struct input_instr {
  uint64_t ip = 0;

  uint8_t is_branch = 0;
  uint8_t branch_taken = 0;

  uint8_t destination_registers[NUM_INSTR_DESTINATIONS] = {};
  uint8_t source_registers[NUM_INSTR_SOURCES] = {};

  uint64_t destination_memory[NUM_INSTR_DESTINATIONS] = {};
  uint64_t source_memory[NUM_INSTR_SOURCES] = {};
};

struct cloudsuite_instr {
  // instruction pointer or PC (Program Counter)
  unsigned long long ip = 0;

  // branch info
  unsigned char is_branch = 0;
  unsigned char branch_taken = 0;

  unsigned char destination_registers[NUM_INSTR_DESTINATIONS_SPARC] = {}; // output registers
  unsigned char source_registers[NUM_INSTR_SOURCES] = {};                 // input registers

  unsigned long long destination_memory[NUM_INSTR_DESTINATIONS_SPARC] = {}; // output memory
  unsigned long long source_memory[NUM_INSTR_SOURCES] = {};                 // input memory

  unsigned char asid[2] = {std::numeric_limits<unsigned char>::max(), std::numeric_limits<unsigned char>::max()};
};

#endif

