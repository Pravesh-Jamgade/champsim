#ifndef DATAMODEL_H
#define DATAMODEL_H
#include <array>
#include <iostream>
#include "user.h"
#include "hist.h"
#include <iomanip>
#include <string>

using namespace std;

static string hit_where_str[CACHE_ID_END+1] = {"LLC", "L2", "L1D", "L1I", "STLB", "DTLB", "ITLB",  "DRAM", "WQ", "x"};

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

enum MISS
{
    COM=0,
    CONF,
    CAP,
    MISS_END
};

// We need this to see if block is evicted earlier.
// It is different that accessed again.
// Victima starts PTW when PTE is evicted. I have another tracker which updates
// its history upon fill, hence looking up that history upon eviction will hit in history
// but i need to see if its been evicted already hence i am using eviction history
class EvictionTracker
{
public:
    struct data {
        int type=DataType::INVALID;
        int evicts = 0;
    };

using Key = std::tuple<uint64_t, int>;
    std::map<Key, data> pte_eviction_tracker;
    EvictionTracker();

    std::pair<std::map<Key, data>::iterator, bool>
    func_track_eviction_data(uint64_t v_addr, int cpuid, DataType dtype);

    bool func_lookup_eviction_data(uint64_t v_addr, int cpuid);
};

class FillTracker
{
    public:
    struct data {
        int type=DataType::INVALID;
        int fills = 0;
    };
    
    using Key = std::tuple<uint64_t, int>;
    // track working set for counting capacity misses
    // vaddress, cpuid -- bitset
    map<Key, data> page_and_cache_block_tracker;

    FillTracker();
    
    // True --> inserted because it is not found, False --> not-inserted as already exists
    std::pair<std::map<Key, data>::iterator, bool> 
    func_track_fill_data(uint64_t v_addr, int cpuid, DataType dtype);
 
    std::pair<std::map<Key, data>::iterator, bool>  
    func_lookup_fill_data(uint64_t v_addr, int cpuid);     
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

    CacheDataModel(string name, uint32_t cpu, uint32_t NUM_WAY):name(name), cpu(cpu)
    {
        eviction_tracker_obj = EvictionTracker();
        fill_tracker_obj = FillTracker();

        for(int i=0; i< REJECTED; i++)
        {
            rd_queue[i] = wr_queue[i] = pf_queue[i] = mshr_queue[i] = 0;
        }

        category_of_misses = (int*)malloc(sizeof(int*) *  4);
        for(int i=0; i< 5; i++)
            category_of_misses[i] = 0;


        // initalize histogram for access latency
        {
            vector<pair<int,int>> exception_bounds;
            exception_bounds.push_back({50, 100});
            exception_bounds.push_back({101, 150});
            exception_bounds.push_back({151, 200});
            exception_bounds.push_back({201, 0x7fffffff});

            hit_access_latency = new Hist(1,5,8,exception_bounds);
            victima_access_latency_at_l2 = new Hist(1,5,8,exception_bounds);
            miss_access_latency = new Hist(1,5,8,exception_bounds);
            miss_fulfilled_latency = new Hist(1,5,8,exception_bounds);
            victima_miss_latency_at_stlb = new Hist(1,5,10,exception_bounds);
        }

        // initalize histogram for reuse distance
        {
            vector<pair<int,int>> exception_bounds;
            exception_bounds.push_back({1e3, 1e4});
            exception_bounds.push_back({1e5, 1e6});
            recall_distance = new Hist(1, 49, 10, exception_bounds);
        }

        {
            vector<pair<int,int>> exception_bounds;
            exception_bounds.push_back({1,1});
            exception_bounds.push_back({2,2});
            exception_bounds.push_back({3,3});
            exception_bounds.push_back({4,4});
            exception_bounds.push_back({5,5});
            exception_bounds.push_back({6,6});
            exception_bounds.push_back({7,7});
            exception_bounds.push_back({8,8});
            sector_block_occupancy = new Hist(0,0,0,exception_bounds);
        }

        {
            vector<pair<int,int>> exception_bounds;
            exception_bounds.push_back({1,1});
            exception_bounds.push_back({2,2});
            exception_bounds.push_back({3,3});
            exception_bounds.push_back({4,4});
            exception_bounds.push_back({5,5});
            exception_bounds.push_back({6,6});
            exception_bounds.push_back({7,7});
            exception_bounds.push_back({8,8});
            exception_bounds.push_back({50,100});
            exception_bounds.push_back({101,150});
            exception_bounds.push_back({151,200});
            exception_bounds.push_back({201, 10000});
            page_reuse_hist = new Hist(2, 5, 10, exception_bounds);
            tblock_reuse_hist = new Hist(2, 5, 10, exception_bounds);
            fill_hist = new Hist(2, 5, 10, exception_bounds);
        }

        {
            vector<pair<int,int>> exception_bounds;
            exception_bounds.push_back({1,1});
            exception_bounds.push_back({2,2});
            exception_bounds.push_back({3,3});
            exception_bounds.push_back({4,4});
            exception_bounds.push_back({5,5});
            exception_bounds.push_back({6,6});
            exception_bounds.push_back({7,7});
            exception_bounds.push_back({8,8});
            exception_bounds.push_back({50,100});
            exception_bounds.push_back({101,150});
            exception_bounds.push_back({151,200});
            exception_bounds.push_back({201, 10000});
            eviction_hist = new Hist(2,5,10,exception_bounds);
        }
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

    int* category_of_misses;
    map<uint64_t,uint64_t> hist_set_conflict_events;  
    map<int, int> hist_recall_distance;

    map<CACHE_ID, int> readmiss_hitwhere;

    // reuse distance hitogram object
    Hist* recall_distance;

    // cacheblock, cpu -- freq
    map<tuple<uint64_t, int>, uint64_t> page_reuse_helper_for_hist;
    Hist* page_reuse_hist;
    Hist* tblock_reuse_hist;

    // miss recorded to fulfill mshr
    Hist* miss_fulfilled_latency;
    // req miss access latency histogram object
    Hist* miss_access_latency;
    // req hit access latency histogram object
    Hist* hit_access_latency;
    // req hit access latency histogram for victima read packet
    Hist* victima_access_latency_at_l2;
    // req miss victima latency
    Hist* victima_miss_latency_at_stlb;

    Hist* sector_block_occupancy;

    EvictionTracker eviction_tracker_obj;
    Hist* eviction_hist;

    FillTracker fill_tracker_obj;
    Hist* fill_hist;

    // type of cache blocks
    int block_type_counters[DataType::DataType_end] = {0};
    string data_type_str[DataType::DataType_end] = {"Data", "PTE", "PMD", "PUD", "PGD", "PRE", "INV"};

    void func_page_block_reuse_helper(string NAME)
    {
        cout << "====================================================\n";
        for(auto entry: eviction_tracker_obj.pte_eviction_tracker)
        {
            eviction_hist->add_data_freq(entry.second.evicts, 1);
        }
        cout << NAME << " Page or Block Repeated Evictions \n";
        eviction_hist->print_histogram("page-or-block-repeated-eviction");

        bool is_in_cache = NAME.find("TLB") == string::npos;

        for(auto entry: fill_tracker_obj.page_and_cache_block_tracker)
        {
            // reuse value of page/cache-block
            int data = entry.second.fills;
            // frequency of such page/cache-blocks
            if(data>0)
            page_reuse_hist->add_data_freq(data, 1);

            if(is_in_cache && entry.second.type== 1)
                tblock_reuse_hist->add_data_freq(data, 1);
            
        }
        cout << NAME << " Page or Block Repeated Fills for Use \n";
        page_reuse_hist->print_histogram("page-or-block-reuse");

        if(is_in_cache)
        {
            cout << NAME << " tblock Repeated Fills for Use \n";
            tblock_reuse_hist->print_histogram("tblock-reuse");
        }
        

        cout << "====================================================\n";
    }

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
        
        cout << tag << "Miss Rate, " << ((double)(rd_queue[Basic::MISS] + wr_queue[Basic::MISS] + pf_queue[Basic::MISS]) * 100 /(double) (rd_queue[Basic::ACCESS] + wr_queue[Basic::ACCESS] + pf_queue[Basic::ACCESS])) << '\n';
    

        cout << "readmiss --> mshr_full\n";
        cout << tag << AdvStat_str[AdvStat::CASCADE_STALL_READLIKEMISS_MSHR_FULL] << ", " << adv_stats[AdvStat::CASCADE_STALL_READLIKEMISS_MSHR_FULL] << '\n';

        cout << "readmiss --> mshr_avail --> nextlevel_full\n";
        cout << tag << AdvStat_str[AdvStat::CASCADE_STALL_READLIKEMISS_NEXTLEVEL_FULL] << ", " << adv_stats[AdvStat::CASCADE_STALL_READLIKEMISS_NEXTLEVEL_FULL] << '\n';

        cout << "mshr write --> eviction_writeback --> nextlevel_full\n";
        cout << tag << AdvStat_str[AdvStat::CASCADE_STALL_FILLLIKEMISS_NEXTLEVEL_FULL] << ", " << adv_stats[AdvStat::CASCADE_STALL_FILLLIKEMISS_NEXTLEVEL_FULL] << '\n';
        
        cout << '\n';

        cout << tag << "Capacity miss, " << category_of_misses[MISS::CAP] << '\n';
        cout << tag << "Compulsory miss, " << category_of_misses[MISS::COM] << '\n';
        cout << tag << "Conflict miss, " << category_of_misses[MISS::CONF] << '\n';
        
        // tracking frequency from corresponding sets
        map<uint64_t, uint64_t> hist_set_conflict_data;
        uint64_t no_of_nonconflict_sets = 0;

        for(auto entry: hist_set_conflict_events)
        {
            hist_set_conflict_data[entry.second]++;
            if(entry.second == 0)
                no_of_nonconflict_sets++;
        }

        // cout << tag << "set conflict stats (evictions and number of such sets)\n";
        // for(auto entry: hist_set_conflict_data)
        //     cout << entry.first << ", " << setw(5) << entry.second << '\n';

        cout << tag << "non-conflict sets, " << no_of_nonconflict_sets << '\n';
        
        cout << tag << "Recall distance BucketBounds and Frequency\n";
        recall_distance->print_histogram(tag);

        cout << tag << "RQ-Hit latency BucketBounds and Frequency\n";
        hit_access_latency->print_histogram(tag);

        cout << tag << "RQ-Miss-Fill latency BucketBounds and Frequency\n";
        miss_access_latency->print_histogram(tag);

        cout << tag << "Waiting Period MSHR-ResolveMSHR latency BucketBounds and Frequency\n";
        miss_fulfilled_latency->print_histogram(tag);
        
        if(tag.find("L2")!=string::npos)
        {
            cout << "L2 Victima Hit access latency BucketBounds and Frequency\n";
            victima_access_latency_at_l2->print_histogram(tag);
        }
        else if(tag.find("STLB")!=string::npos)
        {
            cout << "STLB Victima miss access latency BucketBounds and Frequency\n";
            victima_miss_latency_at_stlb->print_histogram(tag);
        }

        cout <<"\n"<<tag<< " readmiss hit where \n";
        for(auto entry: readmiss_hitwhere)
        {
            cout << hit_where_str[entry.first] << ", " << entry.second << '\n';
        }
        cout << '\n';

        cout << "\n" << tag << " block type\n";
        for(int i=0; i< DataType::DataType_end; i++)
        {
            cout << data_type_str[i] << ", " << block_type_counters[i] << '\n';
        }
        cout << '\n';

        cout << "\n" << tag << " sector block occupancy\n";
        if(name.find("STLB") != string::npos)
        sector_block_occupancy->print_histogram(tag);

        cout << "\n";
        func_page_block_reuse_helper(name);
    }
};

// actual PSC Level initalized with base index 1, but when they send the requests in memory hierary they make it 0 indexed.
enum PSCLevel
{
    PSCL2=1,
    PSCL3,
    PSCL4,
    PSCL5,
    PSCL_END
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
        page_fault = vector<uint64_t>(5,0);
    }

    PTWDataModel(uint32_t cpu): cpu(cpu)
    {
        for(int i=0; i< REJECTED; i++)
        {
            queue_basic_metric[i] = 0;
        }
        page_fault = vector<uint64_t>(5,0);
        matrix_cache_to_ptwlevel_hits = vector<vector<uint64_t>>(PSCL_END, vector<uint64_t>(CACHE_ID_END+1, 0));
    }

    // count psc level hit count. If hit in pscl5, says we have base address for next_level. And we dont need separate memory access
    // for pscl5. For 1 miss in STLB: Not hit in any pscl --> 4 memory access, Hit in pscl5 --> 3 memory access, Hit in pscl4 --> 2 memory access
    uint64_t queue_psc_hit_metric[PSCLevel::PSCL_END] = {0};
    uint64_t queue_psc_miss_metric[PSCLevel::PSCL_END] = {0};
    string PSCL_Hit_str[PSCL_END] = {"*", "pscl2_", "pscl3_", "pscl4_", "pscl5_"};

    uint64_t queue_basic_metric[Basic::BASIC_END] = {0};

    // packet removed from mshr
    uint64_t packet_processed = 0;
    // total miss latency of packet experienced during waiting in mshr queue
    uint64_t packet_processed_total_miss_latency = 0;

    // count of packets processed (not removed actually but packet is again reused for next lower psc level by adding it to end of MSHR)
    uint64_t psc_level_packet_processed[PSCLevel::PSCL_END] = {0};
    // total miss latency experienced by packet waiting at each psc level in mshr
    uint64_t psc_level_packet_processed_miss_latency[PSCLevel::PSCL_END] = {0};

    string pscl_packet_processed_str[PSCL_END] = {"*", "pscl2_avg_miss_latency", "pscl3_avg_miss_latency", "pscl4_avg_miss_latency", "pscl5_avg_miss_latency"};

    // page-faults at each level of radix tree (psc level)
    vector<uint64_t> page_fault;

    map<CACHE_ID, int> readmiss_hitwhere;
    vector<vector<uint64_t>> matrix_cache_to_ptwlevel_hits;
 
    uint32_t cpu =0;

    void print_stats()
    {
        string tag = "cpu" + to_string(cpu) + "_PTW" + " ";
        for(int i=0; i< Basic::BASIC_END; i++)
        {
            cout << tag << Basic_str[i] << ", " << queue_basic_metric[i] << '\n';
        }

        for(int i=0; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << PSCL_Hit_str[i] << "HIT, " << queue_psc_hit_metric[i] << '\n';
        }

        for(int i=0; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << PSCL_Hit_str[i] << "MISS, " << queue_psc_miss_metric[i] << '\n';
        }

        for(int i=0; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << PSCL_Hit_str[i] << "Fault, " << page_fault[i] << '\n';
        }

        for(int i=0; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << pscl_packet_processed_str[i] << ", " << ((double)psc_level_packet_processed_miss_latency[i]/psc_level_packet_processed[i]) << '\n';
        }

        cout << tag << "avg miss latency, " << ((double)packet_processed_total_miss_latency/packet_processed) << '\n';

        cout <<"\n"<<tag<< " readmiss hit where \n";
        for(auto entry: readmiss_hitwhere)
        {
            cout << hit_where_str[entry.first] << ", " << entry.second << '\n';
        }
        cout << '\n';
        
    //      // Print column headers
    //   cout << setw(6) << " " << "|";
    //   for (int i = 0; i < transition_hitmap_for_pp_page[0].size(); ++i) {
    //       cout << setw(3) << i;
    //   }
    //   cout << '\n';

    //   // Print separator line
    //   cout << string(6, '-') << "+";
    //   for (int i = 0; i < transition_hitmap_for_pp_page[0].size(); ++i) {
    //       cout << string(4, '-');
    //   }
    //   cout << '\n';

    //   // Print each row
    //   for (int i = 0; i < 8; ++i) {
    //       cout << setw(6) << i << "|";
    //       for (int j = 0; j < transition_hitmap_for_pp_page[i].size(); ++j) {
    //           cout << setw(3) << transition_hitmap_for_pp_page[i][j] << ',';
    //       }
    //       cout << '\n';
    //   }

        cout <<setw(7)<<" "<< "|";
        for(int i=0; i< matrix_cache_to_ptwlevel_hits[0].size(); i++)
            cout << setw(7) << hit_where_str[i];
        cout << '\n';
        
        cout << string(7, '-') << "+";
        for(int i=0; i< matrix_cache_to_ptwlevel_hits[0].size(); i++)
            cout << string(7, '-');
        cout << '\n';
         
        for(int i=1; i< matrix_cache_to_ptwlevel_hits.size(); i++)
        {
            auto row = matrix_cache_to_ptwlevel_hits[i];
            if(i==0)
                continue;
        
            cout << setw(7) << PSCL_Hit_str[i] << "|";
            for(auto ele: row)
            {
                cout << setw(7) << ele << ", ";
            }
            cout << '\n';
        }
        
        cout << '\n';

    }

};

class O3_DataModel
{
    public:
    
};

#endif