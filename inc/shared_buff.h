#ifndef SHARE
#define SHARE
#include <cstdint>
#include <cstddef>
#include "trace_instruction.h"
const int TRACE_BUF_CAP=1024;

// const int NUM_INSTR_DESTINATIONS_SPARC = 4;
// const int NUM_INSTR_DESTINATIONS = 2;
// const int NUM_INSTR_SOURCES = 4;

class Trace
{
    public:
    Trace(){}
    unsigned long long ip = 0;

    // branch info
    unsigned char is_branch = 0;
    unsigned char branch_taken = 0;

    unsigned char destination_registers[NUM_INSTR_DESTINATIONS] = {}; // output registers
    unsigned char source_registers[NUM_INSTR_SOURCES] = {};           // input registers

    unsigned long long destination_memory[NUM_INSTR_DESTINATIONS] = {}; // output memory
    unsigned long long source_memory[NUM_INSTR_SOURCES] = {};           // input 
    
    char context = '0';        // input 
};

typedef struct  {
    size_t head; // writer index
    size_t tail; // reader index
    Trace buffer[TRACE_BUF_CAP];
} shared_buffer;

// shared_buffer* buf;

#endif