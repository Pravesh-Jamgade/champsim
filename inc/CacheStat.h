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
        
        evicts=devicts=ievicts=0;

        invalid_writes=invalid_evicts=0;
        invalid_fill=invalid_writeback=0;
    }

    /*
        exmample, if your catching all writes at LLC then writebacks here implies writes from L2 not the writeback because eviction from LLC those will be evicts.
        1.[ PACKET_TYPE (ipkt=0/dpkt=2) x WRITE (fill=0/wb=1) ] => [ ipkt+fill=0 / ipkt+wb=1 / dpkt+fill=2 / dpkt+wb=3 ]
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

    /*
    1.count evicts from LLC
    2.count what pc's that evicted block belongs to (instruction/data)x(write-live/write-dead)
    */
    void increase_evicts(IntPtr pc, PACKET_TYPE pkt_type){
        evicts++;
        switch(pkt_type){
            case PACKET_TYPE::DPACKET:
                devicts++;
                break;
            case PACKET_TYPE::INVALID:
                invalid_evicts++;
                break;
            case PACKET_TYPE::IPACKET:
                ievicts++;
        }
    }

    void add_addr(IntPtr addr, PACKET_TYPE pkt_type){
        if(pkt_type == PACKET_TYPE::IPACKET){
            if(uniq_i.find(addr)!=uniq_i.end()){
                uniq_i.erase(addr);
            }
            else uniq_i.insert(addr);
        }
        else if(pkt_type == PACKET_TYPE::DPACKET){
            if(uniq_d.find(addr)!=uniq_d.end()){
                uniq_d.erase(addr);
            }
            else uniq_d.insert(addr);
        }
        else{
            if(uniq_inv.find(addr)!=uniq_inv.end()){
                uniq_inv.erase(addr);
            }
            else uniq_inv.insert(addr);
        }
        
    }

    void increase_invalid_inserts(WRITE write){
        invalid_writes++;
        switch(write){
            case WRITE::FILL:
                invalid_fill++;
                break;
            case WRITE::WRITE_BACK:
                invalid_writeback++;
                break;
        }
    }

    void print(){
        cout << "******************\n";
        cout << "invalid writes: " << invalid_writes << '\n';
        cout << "******************\n";
        string s = "llc_write_type.log";
        fstream f = Log::get_file_stream(s);
        f<<"iwrite,dwrite,iwrite_back,dwrite_back,ifills,dfills,uniq_i,uniq_d\n";
        f <<iwrites<<","<<dwrites <<","<<iwrite_back<<","<<dwrite_back<<","<<ifills<<","<<dfills<<","<<uniq_i.size()<<","<<uniq_d.size()<<'\n';

        f << "ALL INSERTS\n";
        f << "instructions\n";
        f << "iwrite, iwrite_back, ifills\n";
        f <<iwrites<<","<<iwrite_back<<","<<ifills<<"\n";

        f << "data\n";
        f << "dwrite, dwrite_back, dfills\n";
        f <<dwrites<<","<<dwrite_back<<","<<dfills<<","<<"\n";

        f << "invalid\n";
        f << "invalid_write, invalid_writeback, invalid_fills\n";
        f <<invalid_writes<<","<<invalid_writeback<<","<<invalid_fill<<"\n";

        f << "ALL EVICTS\n";
        f << "evicts, ievicts, devicts, invalid_evicts\n";
        f << evicts <<","<<ievicts<<","<<devicts<<","<<invalid_evicts<<"\n";

        f << "ALL UNIQUE\n";
        f << "uniq_i, uniq_d\n";
        f << uniq_i.size() << "," << uniq_d.size() << '\n';
       
        f.close();
    }

    private:
    // to LLC
    IntPtr iwrites, dwrites;
    IntPtr ifills, dfills;
    IntPtr iwrite_back, dwrite_back;
    
    // from LLC
    IntPtr evicts;
    IntPtr devicts, ievicts;
    
    // from LLC
    IntPtr invalid_writes, invalid_evicts;
    // to LLC
    IntPtr invalid_fill, invalid_writeback;

    // at LLC
    set<IntPtr> uniq_i, uniq_d, uniq_inv;
};

#endif