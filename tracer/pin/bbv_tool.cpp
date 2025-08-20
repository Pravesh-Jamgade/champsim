#include "pin.H"
#include <fstream>
#include <iostream>
#include <unordered_map>

static UINT64 instruction_count = 0;
static UINT64 interval_size = 100000000; // 100M instructions per interval
static UINT64 current_interval = 0;
static std::ofstream bbv_out("bbv.out");
static std::ofstream map_out("bb_map.txt");

static std::unordered_map<ADDRINT, UINT32> bb_id_map;
static std::unordered_map<UINT32, UINT64> bbv;
static UINT32 next_bb_id = 0;

VOID CountBBl(UINT32 bb_id, UINT32 insts) {
    instruction_count += insts;
    bbv[bb_id] += 1;

    if (instruction_count >= interval_size) {
        // Output interval
        bbv_out << "T" << current_interval << "BBV";
        for (const auto &entry : bbv) {
            bbv_out << ":" << entry.first << ":" << entry.second << " " ;
        }
        bbv_out << "\n";
        bbv.clear();
        instruction_count = 0;
        current_interval++;
    }
}

VOID Trace(TRACE trace, VOID *v) {
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        ADDRINT addr = BBL_Address(bbl);
        UINT32 num_insts = BBL_NumIns(bbl);

        UINT32 bb_id;
        auto it = bb_id_map.find(addr);
        if (it == bb_id_map.end()) {
            bb_id = next_bb_id++;
            bb_id_map[addr] = bb_id;
            map_out << bb_id << " " << std::hex << addr << std::dec << "\n";
        } else {
            bb_id = it->second;
        }

        BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)CountBBl,
                       IARG_UINT32, bb_id,
                       IARG_UINT32, num_insts,
                       IARG_END);
    }
}

VOID Fini(INT32 code, VOID *v) {
    if (!bbv.empty()) {
        bbv_out << "T" << current_interval << "BBV:";
        for (const auto &entry : bbv) {
            bbv_out << " " << entry.first << " " << entry.second;
        }
        bbv_out << "\n";
    }
    bbv_out.close();
    map_out.close();
}

int main(int argc, char *argv[]) {
    if (PIN_Init(argc, argv)) {
        std::cerr << "Pin initialization failed.\n";
        return 1;
    }

    TRACE_AddInstrumentFunction(Trace, nullptr);
    PIN_AddFiniFunction(Fini, nullptr);
    PIN_StartProgram(); // Never returns

    return 0;
}
