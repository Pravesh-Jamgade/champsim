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

class Count{
    public:
    IntPtr insert[2]={0};
    IntPtr evict[2]={0};
    Count();
};

class AAinfo{
    public:

    std::map<IntPtr, Count> count;

    AAinfo(){}
    
    // insert: true, evict: false
    void insert(std::string Name, IntPtr addr, bool req){
        bool l2 = Name.find("L2")!=std::string::npos;
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
                count[addr]=Count();
            }
        }
        else{
            return ;
        }
    }
};
