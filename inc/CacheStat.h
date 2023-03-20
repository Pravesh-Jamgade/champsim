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

/*
Command class to uphold any counter values;
For writes at set as well we will be using these counter values
*/
class Counter{
    public:
    // to LLC (because we only targeting writebacks and fills LLC)
    IntPtr iwrites, dwrites;
    IntPtr ifills, dfills;
    IntPtr iwrite_back, dwrite_back;
    
    // from LLC (because we only targeting evicts from LLC)
    IntPtr evicts;
    IntPtr devicts, ievicts;

    // set
    // total
    IntPtr total_writes, total_reads;

    Counter(){
        iwrites=dwrites=0;
        ifills=dfills=0;
        iwrite_back=dwrite_back=0;
        evicts=devicts=ievicts=0;

        // set
        total_writes = 0;
        total_reads = 0;
    }
    
    // cache
    IntPtr get_total_writes(){return iwrite_back+dwrite_back+ifills+dfills;}
};

class SetStatus{
    private:
        Counter counter;
    public:
    SetStatus(){
        counter = Counter();
    }
    void increase_write_count(){counter.total_writes++;}
    void increase_read_count(){counter.total_reads++;}
    IntPtr get_writes(){return counter.total_writes;}
    IntPtr get_reads(){return counter.total_reads;}
};

class CacheStat{
    public:
    
    CacheStat(){
        counter = new Counter();
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
                counter->dfills++;
                counter->dwrites++;
                break;
            case WRITE::DWRITEBACK:
                counter->dwrite_back++;
                counter->dwrites++;
                break;
            case WRITE::IFILL:
                counter->ifills++;
                counter->iwrites++;
                break;
            case WRITE::IWRITEBACK:
                counter->iwrite_back++;
                counter->iwrites++;
                break;
        }
    }

    /*
    1.count evicts from LLC
    2.count what pc's that evicted block belongs to (instruction/data)x(write-live/write-dead)
    */
    void increase_evicts(IntPtr pc, PACKET_TYPE pkt_type){
        counter->evicts++;
        switch(pkt_type){
            case PACKET_TYPE::DPACKET:
                counter->devicts++;
                break;
            case PACKET_TYPE::INVALID:
                invalid_evicts++;
                break;
            case PACKET_TYPE::IPACKET:
                counter->ievicts++;
        }
    }

    void add_addr(IntPtr addr, PACKET_TYPE pkt_type){
        if(seen_before.find(addr)!=seen_before.end()) return;

        if(pkt_type == PACKET_TYPE::IPACKET){
            if(uniq_i.find(addr)!=uniq_i.end()){
                uniq_i.erase(addr);
                seen_before.insert(addr);
            }
            else uniq_i.insert(addr);
        }
        else if(pkt_type == PACKET_TYPE::DPACKET){
            if(uniq_d.find(addr)!=uniq_d.end()){
                uniq_d.erase(addr);
                seen_before.insert(addr);
            }
            else uniq_d.insert(addr);
        }
        else{
            if(uniq_inv.find(addr)!=uniq_inv.end()){
                uniq_inv.erase(addr);
                seen_before.insert(addr);
            }
            else uniq_inv.insert(addr);
        }
        
    }

    /*
    It was used to check if all blocks/packets are being labelled correclty either as invalid / i / d.
    If in the end of simualtion if  invalid != 0, then some blocks are not labelled or missed.
    As of now it is correct, we can remove all invalid related booking
    */
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

    /*
    1.update write counter on set
    */
    void update_setstatus_on_write(int set){
        if(set_status.find(set)!=set_status.end()){
            set_status[set].increase_write_count();
        }
        else{
            set_status[set]=SetStatus();
        }
    }
    void update_setstatus_on_read(int set){
        if(set_status.find(set)!=set_status.end()){
            set_status[set].increase_read_count();
        }
        else{
            set_status[set]=SetStatus();
        }
    }

    bool is_set_write_intensive(int set){
        double avg = (double)counter->get_total_writes()/(double)set_status.size();
        IntPtr set_writes=0;
        if(set_status.find(set)!=set_status.end()){
            set_writes = set_status[set].get_writes();
        }
        if(set_writes > avg) return true; // write intensize set
        return false; //else not
    }

    /*
    1.print cache status only 
    */
    void print(){
        cout << "******************\n";
        cout << "invalid writes: " << invalid_writes << '\n';
        cout << "******************\n";
        
        // print cache stuff
        {
            string s = "llc_write_type.log";
            fstream f = Log::get_file_stream(s);
        
            f << "ALL INSERTS\n";
            f << "instructions\n";
            f << "iwrite, iwrite_back, ifills\n";
            f <<counter->iwrites<<","<<counter->iwrite_back<<","<<counter->ifills<<"\n";

            f << "data\n";
            f << "dwrite, dwrite_back, dfills\n";
            f <<counter->dwrites<<","<<counter->dwrite_back<<","<<counter->dfills<<","<<"\n";

            // We can remove Invailid related bookkeeping, as it is verified and we dont see invalid blocks anymore
            f << "invalid\n";
            f << "invalid_write, invalid_writeback, invalid_fills\n";
            f <<invalid_writes<<","<<invalid_writeback<<","<<invalid_fill<<"\n";

            f << "ALL EVICTS\n";
            f << "evicts, ievicts, devicts, invalid_evicts\n";
            f << counter->evicts <<","<<counter->ievicts<<","<<counter->devicts<<","<<invalid_evicts<<"\n";

            f << "ALL UNIQUE\n";
            f << "uniq_i, uniq_d, uniq_inv\n";
            f << uniq_i.size() << "," << uniq_d.size() << "," << uniq_inv.size() << '\n';
        
            f.close();
        }

        // print set stuff
        {
            string s = "llc_set_status.log";
            fstream f = Log::get_file_stream(s);
            f << "set,writes,reads\n";
            for(auto set: set_status){
                f << set.first << "," << set.second.get_writes() << "," << set.second.get_reads() << "\n"; 
            }
        }
    }

    private:
  
    Counter* counter;
    
    // We can remove Invailid related bookkeeping, as it is verified and we dont see invalid blocks anymore
    // from LLC
    IntPtr invalid_writes, invalid_evicts;
    // to LLC
    IntPtr invalid_fill, invalid_writeback;

    // at LLC
    set<IntPtr> uniq_i, uniq_d, uniq_inv;
    set<IntPtr> seen_before;

    map<IntPtr, SetStatus> set_status; 
};

#endif