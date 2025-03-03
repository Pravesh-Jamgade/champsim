#ifndef DATAMODEL_H
#define DATAMODEL_H
#include <iostream>
#include <map>
#include <set>
#include <iomanip>  

#include "utils.h"
#define MERGE_RANGE 10
static int arr[MERGE_RANGE] = {0,10,20,30,40,50,60,70,80,90};

using namespace std;

enum Basic
{
    REQUESTED=0,
    ADDED,
    MERGED,
    REJECTED,
    WQ_FWD,
    ACCESS,
    HIT,
    MISS,
    BASIC_END
};
static string Basic_str[BASIC_END] = {
    "REQUESTED", "ADDED", "MERGED", "REJECTED", "WQ_FWD", "ACCESS", "HIT", "MISS"
};

enum Stall
{
    OP_PENALTY=0,
    OP_FAIL_PENALTY,
    STALL_END
};
static string Stall_str[STALL_END] = {"OP_Penalty", "OP_Fail_Penalty"};

enum AdvStat
{
    CASCADE_STALL_READLIKEMISS_MSHR_FULL,
    CASCADE_STALL_READLIKEMISS_NEXTLEVEL_FULL,
    CASCADE_STALL_FILLLIKEMISS_NEXTLEVEL_FULL,
    ADVSTAT_END
};

static string AdvStat_str[AdvStat::ADVSTAT_END] = {
    "CS_readlikemiss_stalled_MSHR_FULL",
    "CS_readlikemiss_stalled_Nextlevel_FULL",
    "CS_filllikemiss_stalled_Nextlevel_FULL"
};

enum CacheStat
{
    Total_Write = 0,
    Total_Drop,
    Total_Writeback,

    Load_Write,
    Load_Drop,
    Load_Writeback,

    Prefetch_Write,
    Prefetch_Drop,
    Prefetch_Writeback,

    RFO_Write,
    RFO_Drop,
    RFO_Writeback,

    Translation_Write,
    Translation_Drop,
    Translation_Writeback,

    CacheStat_End
};
static string CacheStat_str[CacheStat::CacheStat_End] = {
                                                        "Total Write", "Total Drop", "Total Writeback", 
                                                        "Load Write", "Load Drop", "Load Writeback", 
                                                        "Prefetch Write", "Prefetch Drop", "Prefetch Writeback",
                                                        "RFO Write", "RFO Drop", "RFO Writeback",
                                                        "Translation Write", "Translation Drop", "Translation Writeback"
                                                        };

static string cache_name_str[CACHE_ID::CACHE_ID_END] = {
    "itlb", "dtlb", "stlb", "l1i", "l1d", "l2", "llc", "buffer", "dram"
};

enum MISS
{
    COM=0,
    CONF,
    CAP,
    MISS_END
};

class CacheDataModel
{
    public:
    CacheDataModel()
    {
        for(int i=0; i< BASIC_END; i++)
        {
            rd_queue[i] = wr_queue[i] = pf_queue[i] = mshr_queue[i] = 0;
        }
    }

    CacheDataModel(string name, uint32_t cpu, size_t sets, size_t ways):name(name), cpu(cpu)
    {
        // last index is for merge count higher than we have kept in arr histogram of get_index func
        MSHR_sublocking_oppo = vector<int>(MERGE_RANGE+1, 0);

        for(int i=0; i< sets; i++)
        {
            hist_set_conflict_events[i]=0;
        }

        for(int i=0; i< BASIC_END; i++)
        {
            rd_queue[i] = wr_queue[i] = pf_queue[i] = mshr_queue[i] = 0;
        }

        category_of_misses = (int*)malloc(sizeof(int*) *  4);
        for(int i=0; i< 5; i++)
            category_of_misses[i] = 0;
        
        type_mshr_queue = (int**)malloc(sizeof(int**) * 5);
        for(int i=0; i< 5; i++)
            type_mshr_queue[i] = (int*) malloc(sizeof(int*)*BASIC_END);

    }

    ~CacheDataModel()
    {
        delete category_of_misses;
    }

    string name;
    uint32_t cpu=0;
    int rd_queue[Basic::BASIC_END] = {0};
    int wr_queue[Basic::BASIC_END] = {0};
    int pf_queue[Basic::BASIC_END] = {0};
    int mshr_queue[Basic::BASIC_END] = {0};

    int rd_queue_stalls[Stall::STALL_END] = {0};
    int wr_queue_stalls[Stall::STALL_END] = {0};
    int pf_queue_stalls[Stall::STALL_END] = {0};
    int mshr_queue_stalls[Stall::STALL_END] = {0};
    int adv_stats[AdvStat::ADVSTAT_END] = {0};
    int cache_stat[CacheStat::CacheStat_End] = {0};  

    map<uint64_t,uint64_t> hist_set_conflict_events;  
    map<int,int> hist_reuse_distance;

    set<uint64_t> unique_page_count;
    
    int* category_of_misses;

    // type x category
    int** type_rd_queue;
    int** type_wr_queue;
    int** type_pf_queue;
    int** type_mshr_queue;

    // frequency table: frequency mshr subblocking opportunity; calculated from counting counting page merges
    // index - page merges
    // val - frequency of merges
    vector<int> MSHR_sublocking_oppo;

    // ret i -> between i-1 to i
    // ret 10 -> more than 90
    int get_sublock_opp_index(int val);
    void print_stats();

    void print_end_stats()
    {
        string tag = name + " ";
        cout << tag << "set conflict stats (evictions and number of such sets)\n";
        
        // tracking frequency from corresponding sets
        map<uint64_t, uint64_t> hist_data;
        uint64_t no_of_nonconflict_sets = 0;

        for(auto entry: hist_set_conflict_events)
        {
            hist_data[entry.second]++;
            if(entry.second == 0)
                no_of_nonconflict_sets++;
        }
        
        for(auto entry: hist_data)
            cout << entry.first << ", " << std::setw(5) << entry.second << '\n';
        
        cout << tag << "non-conflict sets, " << no_of_nonconflict_sets << '\n';

        cout << tag << "reuse distance (reuse and frequency)\n";
        for(auto entry: hist_reuse_distance)
        {
            cout << entry.first << ", " << std::setw(5) << entry.second << '\n';
        }
        
        cout << '\n';
    }
};

class PTWDataModel
{
    public:
    PTWDataModel()
    {
        for(int i=0; i< REJECTED; i++)
        {
            queue_basic_metric[i] = 0;
        }
    }

    PTWDataModel(uint32_t cpu): cpu(cpu)
    {
        for(int i=0; i< REJECTED; i++)
        {
            queue_basic_metric[i] = 0;
        }
    }

    // actual PSC Level initalized with base index 1, but when they send the requests in memory hierary they make it 0 indexed.
    enum PSCLevel
    {
        PSCL2=1,
        PSCL3,
        PSCL4,
        PSCL5,
        PSCL_NO,
        PSCL_END
    };

    // count psc level hit count. If hit in pscl5, says we have base address for next_level. And we dont need separate memory access
    // for pscl5. For 1 miss in STLB: Not hit in any pscl --> 4 memory access, Hit in pscl5 --> 3 memory access, Hit in pscl4 --> 2 memory access
    uint64_t queue_psc_hit[PSCLevel::PSCL_END] = {0};
    uint64_t queue_psc_miss[PSCLevel::PSCL_END] = {0};
    string PSCL_Hit_str[PSCLevel::PSCL_END] = {"#", "pscl2_Hit", "pscl3_Hit", "pscl4_Hit", "pscl5_Hit", "PSCL_all_Miss"};
    string PSCL_Miss_str[PSCLevel::PSCL_END] = {"#","pscl2_Miss send to memory", "pscl3_Miss send to memory", "pscl4_Miss send to memory", "pscl5_Miss send to memory", "PSCL_all_Hit"};
    string psc_level_packet_processed_str[PSCLevel::PSCL_END] = {"#","pscl2_processed", "pscl3_processed", "pscl4_processed", "pscl5_processed", "#"};
    uint64_t queue_basic_metric[Basic::BASIC_END] = {0};

    // packet removed from mshr
    uint64_t packet_processed = 0;
    // total miss latency of packet experienced during waiting in mshr queue
    uint64_t packet_processed_total_miss_latency = 0;

    // count of packets processed (not removed actually but packet is again reused for next lower psc level by adding it to end of MSHR)
    uint64_t psc_level_packet_processed[PSCLevel::PSCL_END] = {0};
    // total miss latency experienced by packet waiting at each psc level in mshr
    uint64_t psc_level_packet_processed_miss_latency[PSCLevel::PSCL_END] = {0};

    string pscl_packet_processed_str[PSCLevel::PSCL_END] = {"#", "pscl2_avg_miss_latency", "pscl3_avg_miss_latency", "pscl4_avg_miss_latency", "pscl5_avg_miss_latency", "#"};

    // page-faults at each level of radix tree (psc level)
    uint64_t page_fault[PSCL_END] = {0};
    string page_fault_str[PSCL_END] = {"leaf page", "pscl2", "pscl3", "pscl4", "pscl5", "no pf"};

    uint32_t cpu =0;

    void print_stats()
    {
        string tag = "cpu" + to_string(cpu) + "_PTW" + " ";
        for(int i=0; i< Basic::BASIC_END; i++)
        {
            cout << tag << Basic_str[i] << ", " << queue_basic_metric[i] << '\n';
        }
        
        cout << "hit and miss in psc levels\n";
        for(int i=0; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << PSCL_Hit_str[i] << ", " << queue_psc_hit[i] << '\n';
        }

        for(int i=1; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << PSCL_Miss_str[i] << ", " << queue_psc_miss[i] << '\n';
        }
        
        cout << "all requests successfully returned with data\n";
        for(int i=1; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << psc_level_packet_processed_str[i] << ", " << psc_level_packet_processed[i] << '\n';
        }

        for(int i=1; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << pscl_packet_processed_str[i] << ", " << ((double)psc_level_packet_processed_miss_latency[i]/psc_level_packet_processed[i]) << '\n';
        }

        cout << tag << "avg miss latency, " << ((double)packet_processed_total_miss_latency/packet_processed) << '\n';

        cout << "\npage fault\n";
        for(int i=0; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << page_fault_str[i] << ", " << page_fault[i] << '\n';
        }
    }

};

enum RefType
    {
        refLOAD=0,
        refSTORE,
        refInstr,
        RefTypeEnd
    };

enum O3_counter
    {
        rob_full=0,lq_full,sq_full,
        rob_full_lq_full, rob_full_sq_full, 
        rob_full_lq_empty, rob_full_sq_empty,
        O3_Count_End
    };

class O3_DataModel
{
    public:
    
    int cpu;

    string str_o3_counter[O3_Count_End] = {
        "ROB_FULL", "LQ_FULL", "SQ_FULL",
        "ROB_FULL_LQ_FULL", "ROB_FULL_SQ_FULL",
        "ROB_FULL_LQ_EMPTY", "ROB_FULL_SQ_EMPTY"
    };

    map<uint64_t, uint64_t> instr_translation_time;
    map<uint64_t, uint64_t> data_translation_time;
    map<uint64_t, uint64_t> icache_access_time;
    map<uint64_t, uint64_t> dcache_access_time;
    map<pair<uint64_t, uint64_t>, uint64_t> hist_trans_plus_access_time;
    int instr_resolved_translations[RefTypeEnd] = {0};
    int data_resolved_translations[RefTypeEnd] = {0};

    map<int,int> chain_freq;
    map<int,int> branch_freq;

    int counter[O3_counter::O3_Count_End] = {0};
    int tlbmiss_cachehit[CACHE_ID::CACHE_ID_END] = {0};
    int pagefault_cachehit[CACHE_ID::CACHE_ID_END] = {0};
    int count_instr_tlbmiss, count_instr_pagefault;
    int count_data_tlbmiss, count_data_pagefault;

    O3_DataModel(){}
    O3_DataModel(int cpu){
        this->cpu = cpu;
        count_instr_tlbmiss = count_instr_pagefault = 0;
        count_data_tlbmiss = count_data_pagefault = 0;
    }
    void print_stats();
    
};

#endif