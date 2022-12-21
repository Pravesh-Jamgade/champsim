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
    IntPtr fill[2]={0};
    IntPtr evict[2]={0};
    IntPtr writeback[2]={0};
    TU(){}
    TU(bool l2, bool req, bool type_insert){
        // insert
        if(req){
            //@l2
            if(l2){
                fill[0]++;
            }
            //@l3
            else{
                if(type_insert)
                    writeback[1]++;
                else
                    fill[1]++;
            }
        }
        // evict
        else{
            //@l2
            if(l2)
                evict[0]++;
            //@l3
            else 
                evict[1]++;
        }
    }
};

class AAinfo{
    public:
    std::map<IntPtr, TU> count;

    std::string fileName="aaexp.log";
    FILE* aa_fs = fopen(fileName.c_str(), "w");

    AAinfo(){}
    
    // insert: true, evict: false, type_insert: false (fill) otherwise writeback
    void insert(std::string Name, IntPtr addr, bool req, bool type_insert=false){
        bool l2 = Name.find("L2")!=std::string::npos;
        bool l3 = Name.find("LLC")!=std::string::npos;
        if(l2 || l3){
            auto find_entry = count.find(addr);
            if(find_entry!=count.end()){
                // L2 
                if(l2){
                    if(req==true){
                        // insert @L2
                        find_entry->second.fill[0]++;                     
                    }
                    else{
                        // evict from L2
                        find_entry->second.evict[0]++;
                    }
                }
                // LLC
                else{
                    if(req==true){
                        if(type_insert)
                            find_entry->second.writeback[1]++;
                        else
                            find_entry->second.fill[1]++;
                    }
                    else{
                        find_entry->second.evict[1]++;
                    }
                }
            }
            else{
                count[addr]=TU(l2,req,type_insert);
            }
        }
        else{
            return ;
        }
    }
    std::vector<std::string> get_log(){
        std::vector<std::string> all_log;
        for(auto e: count){
            std::string st = std::to_string(e.first)+","+std::to_string(e.second.fill[0])+","+std::to_string(e.second.evict[0])+","+std::to_string(e.second.writeback[0])+","+std::to_string(e.second.fill[1])+","+std::to_string(e.second.evict[1])+","+std::to_string(e.second.writeback[1]);
            all_log.push_back(st);
            fprintf(aa_fs, "%s\n", st.c_str());
        }
        return all_log;
    }
};

#endif