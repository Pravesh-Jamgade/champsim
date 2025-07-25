#include "tracereader.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "../inc/shared_buff.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

extern int KNOB_LIVE_INPUT;

tracereader::tracereader(uint8_t cpu, std::string _ts) : cpu(cpu), trace_string(_ts)
{
  std::string last_dot = trace_string.substr(trace_string.find_last_of("."));

  if (trace_string.substr(0, 4) == "http") {
    // Check file exists
    char testfile_command[4096];
    sprintf(testfile_command, "wget -q --spider %s", trace_string.c_str());
    FILE* testfile = popen(testfile_command, "r");
    if (pclose(testfile)) {
      std::cerr << "TRACE FILE NOT FOUND" << std::endl;
      assert(0);
    }
    cmd_fmtstr = "wget -qO- -o /dev/null %2$s | %1$s -dc";
  } else {
    std::ifstream testfile(trace_string);
    if (!testfile.good()) {
      std::cerr << "TRACE FILE NOT FOUND" << std::endl;
      assert(0);
    }
    cmd_fmtstr = "%1$s -dc %2$s";
  }

  if (last_dot[1] == 'g') // gzip format
    decomp_program = "gzip";
  else if (last_dot[1] == 'x') // xz
    decomp_program = "xz";
  else {
    std::cout << "ChampSim does not support traces other than gz or xz compression!" << std::endl;
    assert(0);
  }

  if(KNOB_LIVE_INPUT)
    trace_open(trace_string, 1);
  else 
    trace_open(trace_string);
}

tracereader::~tracereader() { close(); }

template <typename T>
ooo_model_instr tracereader::read_single_instr()
{
  T trace_read_instr;

  if(!KNOB_LIVE_INPUT)
  {
    while (!fread(&trace_read_instr, sizeof(T), 1, trace_file)) {
      // reached end of file for this trace
      std::cout << "*** Reached end of trace: " << trace_string << std::endl;
  
      // close the trace file and re-open it
      close();
      trace_open(trace_string);
    }
    
    ooo_model_instr retval(cpu, trace_read_instr);
    return retval;
  }
  else
  {
    while (buf->tail == buf->head) {
      usleep(10); // buffer empty
    }
    input_instr* te = (input_instr*)&buf->buffer[buf->tail];
    // std::cout <<"print: " <<std::dec<< instr_count << std::hex << ", " << te->ip << '\n';
    __sync_synchronize(); // memory barrier
    buf->tail = (buf->tail + 1) % TRACE_BUF_CAP;
    // copy the instruction into the performance model's instruction format
    ooo_model_instr retval(cpu, *te);
    instr_count++;
    return retval;
  }

}

void tracereader::trace_open(std::string trace_string, int app)
{

  int fd = open("/tmp/trace_shm.xz", O_RDWR, 0666);
  buf = (shared_buffer*) mmap(NULL, sizeof(shared_buffer),
                                            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buf == MAP_FAILED) {
      perror("mmap failed");
      return;
  }

  // char gunzip_command[4096];
  // sprintf(gunzip_command, cmd_fmtstr.c_str(), decomp_program.c_str(), trace_string.c_str());
  // trace_file = popen(gunzip_command, "r");
  // if (trace_file == NULL) {
  //   std::cerr << std::endl << "*** CANNOT OPEN TRACE FILE: " << trace_string << " ***" << std::endl;
  //   assert(0);
  // }
}

void tracereader::trace_open(std::string trace_string)
{
  char gunzip_command[4096];
  sprintf(gunzip_command, cmd_fmtstr.c_str(), decomp_program.c_str(), trace_string.c_str());
  trace_file = popen(gunzip_command, "r");
  if (trace_file == NULL) {
    std::cerr << std::endl << "*** CANNOT OPEN TRACE FILE: " << trace_string << " ***" << std::endl;
    assert(0);
  }
}

void tracereader::close()
{
  if (trace_file != NULL) {
    pclose(trace_file);
  }
}

class cloudsuite_tracereader : public tracereader
{
  ooo_model_instr last_instr;
  bool initialized = false;

public:
  cloudsuite_tracereader(uint8_t cpu, std::string _tn) : tracereader(cpu, _tn) {}

  ooo_model_instr get()
  {
    ooo_model_instr trace_read_instr = read_single_instr<cloudsuite_instr>();

    if (!initialized) {
      last_instr = trace_read_instr;
      initialized = true;
    }

    last_instr.branch_target = trace_read_instr.ip;
    ooo_model_instr retval = last_instr;

    last_instr = trace_read_instr;
    return retval;
  }
};

class input_tracereader : public tracereader
{
  ooo_model_instr last_instr;
  bool initialized = false;

public:
  input_tracereader(uint8_t cpu, std::string _tn) : tracereader(cpu, _tn) {}

  ooo_model_instr get()
  {
    ooo_model_instr trace_read_instr = read_single_instr<input_instr>();

    if (!initialized) {
      last_instr = trace_read_instr;
      initialized = true;
    }

    last_instr.branch_target = trace_read_instr.ip;
    ooo_model_instr retval = last_instr;

    last_instr = trace_read_instr;
    return retval;
  }
};

tracereader* get_tracereader(std::string fname, uint8_t cpu, bool is_cloudsuite)
{
  if (is_cloudsuite) {
    return new cloudsuite_tracereader(cpu, fname);
  } else {
    return new input_tracereader(cpu, fname);
  }
}
