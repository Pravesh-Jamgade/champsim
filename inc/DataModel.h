#ifndef DATAMODEL_H
#define DATAMODEL_H
#include <iostream>
#include "user.h"
#include <iomanip>

using namespace std;

static string hit_where_str[CACHE_ID_END+1] = {"LLC", "L2", "L1D", "L1I", "STLB", "DTLB", "ITLB", "WQ", "x"};

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


class Hist
{
    public:
    // data and frequency
    map<int,int> data_freq;
    // bucket bounds
    vector<pair<int,int>> hits_bounds;
    // count bucket_bound frequncy
    vector<int> hist_distance;

    Hist(){}
    Hist(int start, int width, int count, vector<pair<int,int>>& exceptional_boundries)
    {
        for(int i=0; i< count; i++)
        {
            hits_bounds.push_back({start, start+width});
            start += width+1;
        }

        for(auto entry: exceptional_boundries)
            hits_bounds.push_back(entry);
        
        hist_distance.resize(hits_bounds.size(), 0);
    }

    void add_data_freq(int data, int freq){
        data_freq[data] += freq;
    }

    void custom_add_hist_bounds(vector<pair<int,int>>& bounds){
        hits_bounds = bounds;
        hist_distance.resize(hits_bounds.size(), 0);
    }

    void print_histogram(string tag)
    {
        // data is reuse_distance and its corresponding frequecny
        for(auto data: data_freq)
        {
            // look for bounds to which this reuse distance belongs to
            for(int i=0; i< hits_bounds.size(); i++)
            {
                pair<int,int> bound = hits_bounds[i];

                // if data is within bucket_boundry, sumup its frequcny in final histogram
                if(bound.first <= data.first && data.first <= bound.second)
                {
                    // i'th bucket of histogram
                    hist_distance[i] += data.second;
                }
            }
        }
        // print histogram
        for(int i=0; i< hits_bounds.size(); i++)
        {
            pair<int,int> bound = hits_bounds[i];
            cout << bound.first << " - " << bound.second << ", " <<  hist_distance[i] << '\n';
        }
        cout << '\n';
    }
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
            victima_miss_latency_at_stlb = new Hist(1,5,10,exception_bounds);
        }

        // initalize histogram for reuse distance
        {
            vector<pair<int,int>> exception_bounds;
            exception_bounds.push_back({0, 0});
            exception_bounds.push_back({50, 100});
            exception_bounds.push_back({101, 150});
            exception_bounds.push_back({151, 200});
            exception_bounds.push_back({201, 0x7fffffff});
            reuse_distance = new Hist(1,5,8,exception_bounds);
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
    map<int, int> hist_reuse_distance;

    map<CACHE_ID, int> readmiss_hitwhere;

    // reuse distance hitogram object
    Hist* reuse_distance;
    // req miss access latency histogram object
    Hist* miss_access_latency;
    // req hit access latency histogram object
    Hist* hit_access_latency;
    // req hit access latency histogram for victima read packet
    Hist* victima_access_latency_at_l2;
    // req miss victima latency
    Hist* victima_miss_latency_at_stlb;

    // type of cache blocks
    int block_type_counters[DataType::DataType_end] = {0};
    string data_type_str[DataType::DataType_end] = {"Data", "PTE", "PMD", "PUD", "PGD", "VICTIMA", "INV"};

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
   
        cout << tag << "set conflict stats (evictions and number of such sets)\n";
        
        // tracking frequency from corresponding sets
        map<uint64_t, uint64_t> hist_set_conflict_data;
        uint64_t no_of_nonconflict_sets = 0;

        for(auto entry: hist_set_conflict_events)
        {
            hist_set_conflict_data[entry.second]++;
            if(entry.second == 0)
                no_of_nonconflict_sets++;
        }
        for(auto entry: hist_set_conflict_data)
            cout << entry.first << ", " << setw(5) << entry.second << '\n';
        cout << tag << "non-conflict sets, " << no_of_nonconflict_sets << '\n';

        cout << "Reuse distance BucketBounds and Frequency\n";
        reuse_distance->print_histogram(tag);

        cout << "Miss access latency BucketBounds and Frequency\n";
        miss_access_latency->print_histogram(tag);

        cout << "Hit access latency BucketBounds and Frequency\n";
        hit_access_latency->print_histogram(tag);
        
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

        cout << "\n" << tag << " block type\n";
        for(int i=0; i< DataType::DataType_end; i++)
        {
            cout << data_type_str[i] << ", " << block_type_counters[i] << '\n';
        }
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
    }

    PTWDataModel(uint32_t cpu): cpu(cpu)
    {
        for(int i=0; i< REJECTED; i++)
        {
            queue_basic_metric[i] = 0;
        }
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
    uint64_t page_fault[PSCL_END] = {0};

    map<CACHE_ID, int> readmiss_hitwhere;

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
    }

};

class O3_DataModel
{
    public:
    
};

#endif