#ifndef CPU_STAT_H
#define CPU_STAT_H

#include <bits/stdc++.h>
#include <fstream>
#include "log.h"

class CPUStat{
    public:
    vector<string> heartbeat_info;
    
    CPUStat(){}

    void collect_heartbeat(float value, int cpuid){
        string s = to_string(value) + "," + to_string(cpuid) + "\n";
        cout << s;
        heartbeat_info.push_back(s);
    }

    void print_heartbeats(){
        fstream heart_fs;
        string heart_file = "heartbeat.log";
        heart_fs = Log::get_file_stream(heart_file);
        if(!heart_fs.is_open()){
            heart_fs = Log::get_file_stream(heart_file);
        }
        
        for(auto e: heartbeat_info){
            cout << e;
            heart_fs << e;
        }
    }
};
#endif