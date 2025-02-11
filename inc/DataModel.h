#ifndef DATAMODEL_H
#define DATAMODEL_H
#include <iostream>
#include <map>
#include <set>
#include <iomanip>  

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

enum CACHE_ID{IS_LLC=0, IS_L2, IS_L1D, IS_STLB, IS_DTLB, IS_ITLB, CACHE_ID_END};

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
        for(int i=0; i< REJECTED; i++)
        {
            rd_queue[i] = wr_queue[i] = pf_queue[i] = mshr_queue[i] = 0;
        }
    }

    CacheDataModel(string name, uint32_t cpu, size_t sets, size_t ways):name(name), cpu(cpu)
    {
        for(int i=0; i< sets; i++)
        {
            hist_set_conflict_events[i]=0;
        }

        for(int i=0; i< REJECTED; i++)
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
    uint64_t rd_queue[Basic::BASIC_END] = {0};
    uint64_t wr_queue[Basic::BASIC_END] = {0};
    uint64_t pf_queue[Basic::BASIC_END] = {0};
    uint64_t mshr_queue[Basic::BASIC_END] = {0};

    uint64_t rd_queue_stalls[Stall::STALL_END] = {0};
    uint64_t wr_queue_stalls[Stall::STALL_END] = {0};
    uint64_t pf_queue_stalls[Stall::STALL_END] = {0};
    uint64_t mshr_queue_stalls[Stall::STALL_END] = {0};
    
    uint64_t adv_stats[AdvStat::ADVSTAT_END] = {0};
    uint64_t cache_stat[CacheStat::CacheStat_End] = {0};  

    map<uint64_t,uint64_t> hist_set_conflict_events;  
    map<int,int> hist_reuse_distance;

    set<uint64_t> unique_page_count;
    
    int* category_of_misses;

    // type x category
    int** type_rd_queue;
    int** type_wr_queue;
    int** type_pf_queue;
    int** type_mshr_queue;

    void print_stats()
    {
        string tag = name + " ";

        for(int i=0; i< Basic::BASIC_END; i++)
            cout << tag << Basic_str[i] << " Load Queue, " << rd_queue[i] << '\n';
        
        cout << '\n';
        
        for(int i=0; i< Basic::BASIC_END; i++)
            cout << tag << Basic_str[i] << " Store Queue, " << wr_queue[i] << '\n';

        cout << '\n';

        for(int i=0; i< Basic::BASIC_END; i++)
            cout << tag << Basic_str[i] << " Prefetch Queue, " << pf_queue[i] << '\n';
        
        cout << '\n';

        for(int i=0; i< Basic::BASIC_END; i++)
            cout << tag << Basic_str[i] << " MSHR Queue, " << mshr_queue[i] << '\n';
        
        cout << tag << "Miss Rate, " << ((double)(rd_queue[Basic::MISS] + wr_queue[Basic::MISS] + pf_queue[Basic::MISS]) * 100 /(double) (rd_queue[Basic::ACCESS] + wr_queue[Basic::ACCESS] + pf_queue[Basic::ACCESS])) << '\n';
        
        cout << '\n';
        /////////////////////////////////////////////////////////////////////////////////////

        for(int i=0; i< Stall::STALL_END; i++)
            cout << tag << Stall_str[i] << " Load Queue, " << rd_queue_stalls[i] << '\n';
        
        cout << '\n';

        for(int i=0; i< Stall::STALL_END; i++)
            cout << tag << Stall_str[i] << " Store Queue, " << wr_queue_stalls[i] << '\n';
        
        cout << '\n';

        for(int i=0; i< Stall::STALL_END; i++)
            cout << tag << Stall_str[i] << " Prefetch Queue, " << pf_queue_stalls[i] << '\n';
        
        cout << '\n';

        for(int i=0; i< Stall::STALL_END; i++)
            cout << tag << Stall_str[i] << " MSHR Queue, " << mshr_queue_stalls[i] << '\n';

        cout << '\n';

        for(int i=0; i< CacheStat::CacheStat_End; i++)
        {
            cout << tag << CacheStat_str[i] << ", " << cache_stat[i] << '\n';
        }

        cout << '\n';

        cout << tag << "Capacity miss, " << category_of_misses[MISS::CAP] << '\n';
        cout << tag << "Compulsory miss, " << category_of_misses[MISS::COM] << '\n';
        cout << tag << "Conflict miss, " << category_of_misses[MISS::CONF] << '\n';
        cout << tag << "Unique page count, " << unique_page_count.size() << '\n';

        cout << "readmiss --> mshr_full\n";
        cout << tag << AdvStat_str[AdvStat::CASCADE_STALL_READLIKEMISS_MSHR_FULL] << ", " << adv_stats[AdvStat::CASCADE_STALL_READLIKEMISS_MSHR_FULL] << '\n';

        cout << "readmiss --> mshr_avail --> nextlevel_full\n";
        cout << tag << AdvStat_str[AdvStat::CASCADE_STALL_READLIKEMISS_NEXTLEVEL_FULL] << ", " << adv_stats[AdvStat::CASCADE_STALL_READLIKEMISS_NEXTLEVEL_FULL] << '\n';

        cout << "mshr write --> eviction_writeback --> nextlevel_full\n";
        cout << tag << AdvStat_str[AdvStat::CASCADE_STALL_FILLLIKEMISS_NEXTLEVEL_FULL] << ", " << adv_stats[AdvStat::CASCADE_STALL_FILLLIKEMISS_NEXTLEVEL_FULL] << '\n';
        
        cout << '\n';
    }

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

    O3_DataModel(){}
    O3_DataModel(int cpu){
        this->cpu = cpu;
    }
    void print_stats();
    
};

#endif