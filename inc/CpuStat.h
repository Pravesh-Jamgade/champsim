#ifndef CPU_STAT_H
#define CPU_STAT_H

#include <bits/stdc++.h>
#include <fstream>
#include "log.h"

class CPUStat{
    private:
    fstream heart_fs;
    public:
    CPUStat(){
        string heart_file = "heartbeat.log";
        heart_fs = Log::get_file_stream(heart_file);
    }

    ~CPUStat(){
        heart_fs.close();
    }

    void print_heartbeat(float value, int cpuid){
        heart_fs << cpuid << "," << value << '\n';
        cout << cpuid << "," << value << '\n';
    }
};
#endif