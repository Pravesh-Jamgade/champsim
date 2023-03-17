#ifndef CACHE_STAT_H
#define CACHE_STAT_H


#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include "constant.h"
#include "log.h"

using namespace std;

class CacheStat{
    public:
    
    CacheStat(){
        iwrites=dwrites=0;
        ifills=dfills=0;
        iwrite_back=dwrite_back=0;
    }

    /*
        [ PACKET_TYPE (ipkt=0/dpkt=2) x WRITE (fill=0/wb=1) ] => [ ipkt+fill=0 / ipkt+wb=1 / dpkt+fill=2 / dpkt+wb=3 ]
    */
    void increase(WRITE type){
        switch(type){
            case WRITE::DFILL:
                dfills++;
                dwrites++;
                break;
            case WRITE::DWRITEBACK:
                dwrite_back++;
                dwrites++;
                break;
            case WRITE::IFILL:
                ifills++;
                iwrites++;
                break;
            case WRITE::IWRITEBACK:
                iwrite_back++;
                iwrites++;
                break;
        }
    }

    void add_addr(IntPtr addr, bool instr){
        if(instr){
            if(uniq_i.find(addr)!=uniq_i.end()){
                uniq_i.erase(addr);
            }
            else uniq_i.insert(addr);
        }
        else{
            if(uniq_d.find(addr)!=uniq_d.end()){
                uniq_d.erase(addr);
            }
            else uniq_d.insert(addr);
        }
        
    }

    void print(){
        string s = "llc_write_type.log";
        fstream f = Log::get_file_stream(s);
        f<<"iwrite,dwrite,iwrite_back,dwrite_back,ifills,dfills,uniq_i,uniq_d\n";
        f <<iwrites<<","<<dwrites <<","<<iwrite_back<<","<<dwrite_back<<","<<ifills<<","<<dfills<<","<<uniq_i.size()<<","<<uniq_d.size()<<'\n';
        f.close();
    }

    private:
    IntPtr iwrites, dwrites;
    IntPtr ifills, dfills;
    IntPtr iwrite_back, dwrite_back;
    set<IntPtr> uniq_i, uniq_d;
};

#endif