#ifndef CPU_STAT_H
#define CPU_STAT_H

#include <bits/stdc++.h>
#include <fstream>
#include "log.h"

class CPUStat{
    private:
    vector<string> heartbeat_info;
    public:
    CPUStat(){
        
    }

    ~CPUStat(){
        fstream heart_fs;
        string heart_file = "heartbeat.log";
        heart_fs = Log::get_file_stream(heart_file);
        if(!heart_fs.is_open()){
            heart_fs = Log::get_file_stream(heart_file);
        }
        
        for(auto e: heartbeat_info){
            heart_fs << e;
        }
    }

    void print_heartbeat(float value, int cpuid){
        string s = to_string(value) + "," + to_string(cpuid) + "\n";
        heartbeat_info.push_back(s);
    }
};
#endif