#ifndef DATAMODEL_H
#define DATAMODEL_H
#include <iostream>
using namespace std;

enum Basic
{
    ACCESS =0,
    HIT,
    MISS,
    REQUESTED,
    ADDED,
    MERGED,
    REJECTED,
    BASIC_END
};

static string Basic_str[BASIC_END] = {"ACCESS", "HIT", "MISS", "REQUESTED", "ADDED", "MERGED", "REJECTED"};


class CacheDataModel
{
    public:
    CacheDataModel()
    {
        for(int i=0; i< REJECTED; i++)
        {
            queue_basic_metric[i] = 0;
        }
    }
    uint64_t queue_basic_metric[Basic::BASIC_END] = {0};
};

class PTWDataModel
{
    public:
    PTWDataModel()
    {
        for(int i=0; i< REJECTED; i++)
        {
            queue_basic_metric[Basic::BASIC_END] = 0;
        }
    }

    PTWDataModel(uint32_t cpu): cpu(cpu)
    {
        for(int i=0; i< REJECTED; i++)
        {
            queue_basic_metric[Basic::BASIC_END] = 0;
        }
    }

    // actual PSC Level initalized with base index 1, but when they send the requests in memory hierary they make it 0 indexed.
    enum PSCLevel
    {
        PSCL2=0,
        PSCL3,
        PSCL4,
        PSCL5,
        PSCL_END
    };

    // count psc level hit count. If hit in pscl5, says we have base address for next_level. And we dont need separate memory access
    // for pscl5. For 1 miss in STLB: Not hit in any pscl --> 4 memory access, Hit in pscl5 --> 3 memory access, Hit in pscl4 --> 2 memory access
    uint64_t queue_psc_metric[PSCLevel::PSCL_END] = {0};
    string PSCL_Hit_str[PSCL_END] = {"\\pscl2_Hit", "pscl3_Hit", "pscl4_Hit", "pscl5_Hit"};

    uint64_t queue_basic_metric[Basic::BASIC_END] = {0};

    // packet removed from mshr
    uint64_t packet_processed = 0;
    // total miss latency of packet experienced during waiting in mshr queue
    uint64_t packet_processed_total_miss_latency = 0;

    // count of packets processed (not removed actually but packet is again reused for next lower psc level by adding it to end of MSHR)
    uint64_t psc_level_packet_processed[PSCLevel::PSCL_END] = {0};
    // total miss latency experienced by packet waiting at each psc level in mshr
    uint64_t psc_level_packet_processed_miss_latency[PSCLevel::PSCL_END] = {0};

    string pscl_packet_processed_str[PSCL_END] = {"pscl2_avg_miss_latency", "pscl3_avg_miss_latency", "pscl4_avg_miss_latency", "pscl5_avg_miss_latency"};

    // page-faults at each level of radix tree (psc level)
    uint64_t page_fault[PSCL_END] = {0};

    uint32_t cpu =0;

    void print_stats()
    {
        string tag = "PTW cpu" + to_string(cpu) + " ";
        for(int i=0; i< Basic::BASIC_END; i++)
        {
            cout << tag << Basic_str[i] << ", " << queue_basic_metric[i] << '\n';
        }

        for(int i=0; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << PSCL_Hit_str[i] << ", " << queue_psc_metric[i] << '\n';
        }

        for(int i=0; i< PSCLevel::PSCL_END; i++)
        {
            cout << tag << pscl_packet_processed_str[i] << ", " << ((double)psc_level_packet_processed_miss_latency[i]/psc_level_packet_processed[i]) << '\n';
        }

        cout << tag << "PTW avg miss latency, " << ((double)packet_processed_total_miss_latency/packet_processed) << '\n';
    }

};

#endif