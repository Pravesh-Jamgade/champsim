#ifndef AAINFO_H
#define AAINFO_H
#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <getopt.h>
#include <iomanip>
#include <signal.h>
#include <string.h>
#include <vector>
#include <fstream>
#include <stdio.h>
#include <unistd.h>
#include <map>
#include "constant.h"

class TU{
    public:
    IntPtr insert[2]={0};
    IntPtr evict[2]={0};
    IntPtr wb = 0; // writeback on LLC
    TU(){}
};

class AAinfo{
    public:
    std::map<IntPtr, TU> count;

    string fileName="aaexp.log";
    FILE* aa_fs = fopen(fileName.c_str(), "w");

    AAinfo(){}
    
    // insert: true, evict: false
    void insert(std::string Name, IntPtr addr, bool req){
        bool l2 = false;//Name.find("L2")!=std::string::npos;
        bool l3 = Name.find("LLC")!=std::string::npos;
        if(l2 || l3){
            auto find_entry = count.find(addr);
            if(find_entry!=count.end()){
                // L2 
                if(l2){
                    if(req==true){
                        // insert @L2
                        find_entry->second.insert[0]++;                     
                    }
                    else{
                        // evict from L2
                        find_entry->second.evict[0]++;
                    }
                }
                // LLC
                else{
                    if(req==true){
                        find_entry->second.insert[1]++;
                    }
                    else{
                        find_entry->second.evict[1]++;
                    }
                }
            }
            else{
                count[addr]=TU();
            }
        }
        else{
            return ;
        }
    }
    vector<std::string> get_log(){
        vector<std::string> all_log;
        for(auto e: count){
            std::string st = to_string(e.first)+","+to_string(e.second.insert[0])+","+to_string(e.second.evict[0])+","+to_string(e.second.insert[1])+","+to_string(e.second.evict[1]);
            all_log.push_back(st);
            fprintf(aa_fs, "%s\n", st.c_str());
        }
        return all_log;
    }
};

#endif