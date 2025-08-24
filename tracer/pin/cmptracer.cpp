/*
 *    Copyright 2023 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*! @file
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs
 *  and could serve as the starting point for developing your first PIN tool
 */

#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>

#include "../../inc/trace_instruction.h"
#include "../../inc/shared_buff.h"
#include <sys/mman.h>
#include <fcntl.h>
#include "pin.H"

using trace_instr_format_t = input_instr;

shared_buffer* buf;
FILE* out;
int incr = 0;
/* ================================================================== */
// Global variables
/* ================================================================== */
bool enable_simpoint_reading = 0;
std::vector<std::pair<uint64_t, uint64_t>> simpts;
int running_simptr_status[100] = {0};
int simptr_index = 0;

UINT64 interval_size = 0;

UINT64 instrCount = 0;

std::ofstream outfile;

trace_instr_format_t curr_instr;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "champsim.trace", "specify file name for Champsim tracer output");

KNOB<UINT64> KnobSkipInstructions(KNOB_MODE_WRITEONCE, "pintool", "s", "0", "How many instructions to skip before tracing begins");

KNOB<UINT64> KnobTraceInstructions(KNOB_MODE_WRITEONCE, "pintool", "t", "1000000", "How many instructions to trace");

KNOB<std::string>KnobTraceFileName(KNOB_MODE_WRITEONCE,  "pintool",
  "trace", "/tmp/trace_shm.xz", "Name of trace file");

// All three comes together
KNOB<BOOL> KnobEnableSimpoint(KNOB_MODE_WRITEONCE, "pintool", "enable_simpoint", "0", 
  "Enable simpoint tracing");

KNOB<std::string> KnobReadSimpoint(KNOB_MODE_WRITEONCE, "pintool",     "input",         "simpoint.simptr", 
  "Read simpoint file");

KNOB<UINT64>KnobIntervalSize(KNOB_MODE_WRITEONCE,  "pintool",
    "interval_size", "10000000", "Size of Interval");


/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
  std::cerr << "This tool creates a register and memory access trace" << std::endl
            << "Specify the output trace file with -o" << std::endl
            << "Specify the number of instructions to skip before tracing with -s" << std::endl
            << "Specify the number of instructions to trace with -t" << std::endl
            << std::endl;

  std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;

  return -1;
}

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

void ResetCurrentInstruction(VOID* ip)
{
  curr_instr = {};
  curr_instr.ip = (unsigned long long int)ip;
}

BOOL ShouldWrite()
{
  ++instrCount;
  return (instrCount > KnobSkipInstructions.Value()) && (instrCount <= (KnobTraceInstructions.Value() + KnobSkipInstructions.Value()));
}

void WriteCurrentInstruction()
{
  // typename decltype(outfile)::char_type buf[sizeof(trace_instr_format_t)];
  // std::memcpy(buf, &curr_instr, sizeof(trace_instr_format_t));
  // outfile.write(buf, sizeof(trace_instr_format_t));

  // typename decltype(outfile)::char_type temp[sizeof(trace_instr_format_t)];
  // std::memcpy(temp, &curr_instr, sizeof(trace_instr_format_t));
  // outfile.write(temp, sizeof(trace_instr_format_t));

  size_t next_head = 0;
  next_head = (buf->head + 1) % TRACE_BUF_CAP;
  while(next_head == buf->tail)
  {
      usleep(10);
  }

  std::cout << "print: " << std::dec << incr <<", H, "<<buf->head<<", T, "<<buf->tail << " , " << std::hex << curr_instr.ip << '\n';
  incr++;
  buf->buffer[buf->head] = curr_instr;

  __sync_synchronize();
  buf->head = next_head;
}

void BranchOrNot(UINT32 taken)
{
  curr_instr.is_branch = 1;
  curr_instr.branch_taken = taken;
}

template <typename T>
void WriteToSet(T* begin, T* end, UINT32 r)
{
  auto set_end = std::find(begin, end, 0);
  auto found_reg = std::find(begin, set_end, r); // check to see if this register is already in the list
  *found_reg = r;
}

void readsimpoint()
{
    interval_size = KnobIntervalSize.Value();
    UINT64 simpoint, index;
    while(std::cin>>simpoint>>index)
    {
        --simpoint;
        simpts.push_back({simpoint, simpoint*interval_size});
    }

    for(auto e: simpts)
        std::cout << e.first << " -- " << e.second << '\n';
}

VOID test_and_return(VOID* v)
{
    UINT32 *t = reinterpret_cast<UINT32*>(v);
    for(auto kv: simpts)
    {
        if(kv.first <= instrCount && instrCount <= kv.second)
        {
            *t = 1;
            if(running_simptr_status[simptr_index] == 0)
            {
              running_simptr_status[simptr_index] = 1;
              std::cout << "region: " << kv.first << " to " << kv.second << ", curr="<<*t <<", simptr_index, "<< simptr_index << '\n';
              simptr_index++;
            }
        }
    }
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID* v)
{
  UINT32* enable = new UINT32;
  *enable = 0;
  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)test_and_return, IARG_PTR, (VOID*)enable, IARG_END);

  if(*enable == 0 && KnobEnableSimpoint.Value())
  {
    return;
  }

  // begin each instruction with this function
  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)ResetCurrentInstruction, IARG_INST_PTR, IARG_END);

  // instrument branch instructions
  if (INS_IsBranch(ins))
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)BranchOrNot, IARG_BRANCH_TAKEN, IARG_END);

  // instrument register reads
  UINT32 readRegCount = INS_MaxNumRRegs(ins);
  for (UINT32 i = 0; i < readRegCount; i++) {
    UINT32 regNum = INS_RegR(ins, i);
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteToSet<unsigned char>, IARG_PTR, curr_instr.source_registers, IARG_PTR,
                   curr_instr.source_registers + NUM_INSTR_SOURCES, IARG_UINT32, regNum, IARG_END);
  }

  // instrument register writes
  UINT32 writeRegCount = INS_MaxNumWRegs(ins);
  for (UINT32 i = 0; i < writeRegCount; i++) {
    UINT32 regNum = INS_RegW(ins, i);
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteToSet<unsigned char>, IARG_PTR, curr_instr.destination_registers, IARG_PTR,
                   curr_instr.destination_registers + NUM_INSTR_DESTINATIONS, IARG_UINT32, regNum, IARG_END);
  }

  // instrument memory reads and writes
  UINT32 memOperands = INS_MemoryOperandCount(ins);

  // Iterate over each memory operand of the instruction.
  for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
    if (INS_MemoryOperandIsRead(ins, memOp))
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteToSet<unsigned long long int>, IARG_PTR, curr_instr.source_memory, IARG_PTR,
                     curr_instr.source_memory + NUM_INSTR_SOURCES, IARG_MEMORYOP_EA, memOp, IARG_END);
    if (INS_MemoryOperandIsWritten(ins, memOp))
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteToSet<unsigned long long int>, IARG_PTR, curr_instr.destination_memory, IARG_PTR,
                     curr_instr.destination_memory + NUM_INSTR_DESTINATIONS, IARG_MEMORYOP_EA, memOp, IARG_END);
  }

  // finalize each instruction with this function
  INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)ShouldWrite, IARG_END);
  INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteCurrentInstruction, IARG_END);
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID* v) { outfile.close(); }

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments,
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char* argv[])
{
  // Initialize PIN library. Print help message if -h(elp) is specified
  // in the command line or the command line is invalid
  if (PIN_Init(argc, argv))
    return Usage();

  enable_simpoint_reading = KnobEnableSimpoint.Value();
  if(enable_simpoint_reading)
  {
      freopen(KnobReadSimpoint.Value().c_str(), "r", stdin);
      readsimpoint();
  }

  outfile.open(KnobOutputFile.Value().c_str(), std::ios_base::binary | std::ios_base::trunc);
  if (!outfile) {
    std::cout << "Couldn't open output trace file. Exiting." << std::endl;
    exit(1);
  }

  int fd = open(("/tmp/"+KnobTraceFileName.Value()).c_str(), O_RDWR | O_CREAT, 0666);
  ftruncate(fd, sizeof(shared_buffer));
  buf = (shared_buffer*) mmap(NULL, sizeof(shared_buffer),
                                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buf == MAP_FAILED) {
      perror("mmap failed");
      return 1;
  }

  // Initialize only if needed
  buf->head = 0;
  buf->tail = 0;

  // Register function to be called to instrument instructions
  INS_AddInstrumentFunction(Instruction, 0);

  // Register function to be called when the application exits
  PIN_AddFiniFunction(Fini, 0);

  // Start the program, never returns
  PIN_StartProgram();

  return 0;
}
