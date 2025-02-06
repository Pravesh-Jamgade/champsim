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

}