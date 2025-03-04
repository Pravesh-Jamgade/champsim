#include <bits/stdc++.h>
#include "DataModel.h"
extern int KNOB_MSHR_SUBLOCK;

// return index between 1 to MERGE_RANGE inclusive
int CacheDataModel::get_sublock_opp_index(int val)
{
    for(int i=1; i< MERGE_RANGE; i++)
    {
        if(arr[i-1] < val && val <= arr[i])
            return i;
    }
    if(val < arr[10])
    {
        cout << "merge range invalid\n";
        exit(0);
    }
    return MERGE_RANGE;
}

void CacheDataModel::print_stats()
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

    string str_log = "Merges and Windows";
    for(auto ele: MSHR_sublocking_oppo)
        str_log += to_string(ele) + ", ";
    cout << tag << str_log << '\n'; 

    if(KNOB_MSHR_SUBLOCK)
    {
        for(int i=0; i< SUBBLOCK::CLUSTER_END; i++)
        {
            cout << tag << mshr_sublock_stat_str[i] <<", "<<mshr_sublock_stat[i]<<'\n';
        }
    }

    cout << '\n';
}


void O3_DataModel::print_stats()
{
    string tag = "O3 cpu" + to_string(cpu) + " ";

    // cout << tag << "instruction translation time histogram (time and freq)\n";
    // for(auto entry: instr_translation_time)
    // {
    //     cout << entry.first << ", " << std::setw(5) << entry.second << '\n';
    // }
    // cout << '\n';

    // cout << tag << "data translation time histogram (time and freq)\n";
    // for(auto entry: data_translation_time)
    // {
    //     cout << entry.first << ", " << std::setw(5) << entry.second << '\n';
    // }
    // cout << '\n';

    // cout << tag << "icache access time histogram (time and freq)\n";
    // for(auto entry: icache_access_time)
    //     cout << entry.first << ", " << std::setw(5)<< entry.second << '\n';
    // cout << '\n';

    // cout << tag << "dcache access time histogram (time and freq)\n";
    // for(auto entry: dcache_access_time)
    //     cout << entry.first << ", " << std::setw(5)<< entry.second << '\n';
    // cout << '\n';
    
    cout << tag << "resolved instr translation, " << instr_resolved_translations[refInstr] << '\n';
    cout << tag << "resolved data load translation, " << data_resolved_translations[refLOAD] << '\n';
    cout << tag << "resolved data store translation, " << data_resolved_translations[refSTORE] << '\n';
    cout << '\n';

    // cout << tag << "dependency chain histo (chain length and freq)\n";
    // for(auto entry: chain_freq)
    //     cout << entry.first << ", " << std::setw(5) << entry.second << '\n';
    // cout << '\n';

    // cout << tag << "direct-dependency (len=1) branches histo (count and freq)\n";
    // for(auto entry: branch_freq)
    //     cout << entry.first << ", " << std::setw(5) << entry.second << '\n';

    cout << "(Number of cycles)\n";
    for(int i=0; i< O3_counter::O3_Count_End; i++)
    {
        cout << tag << str_o3_counter[i] << ", " << counter[i] << '\n';
    }
    cout << '\n';

    cout << "Number of hits in cache even when their TLB miss [or upon Page fault miss --> this is highly unlikely]\n";
    cout << "tlbmiss_cachehit and pagefault_cachehit\n";
    for(int i=0; i< CACHE_ID::CACHE_ID_END; i++)
    {
        cout << tag << cache_name_str[i] << ", " << std::setw(3) << tlbmiss_cachehit[i] << ", " << std::setw(5) << pagefault_cachehit[i] << '\n';
    }
    cout << '\n';

    cout << tag << "count of tracked instructions stlbmiss, " << count_instr_tlbmiss << '\n';
    cout << tag << "count of tracked instructions pagefault, " << count_instr_pagefault << '\n';
    cout << tag << "count of tracked data stlbmiss, " << count_data_tlbmiss << '\n';
    cout << tag << "count of tracked data pagefault, " << count_data_pagefault << '\n';
}