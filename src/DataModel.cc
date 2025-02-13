#include <bits/stdc++.h>
#include "DataModel.h"

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