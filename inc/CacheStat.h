#ifndef CACHE_STAT_H
#define CACHE_STAT_H


#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include "block.h"
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
    IntPtr total_writes, total_reads, total_deads, total_alive;

    Counter(){
        iwrites=dwrites=0;
        ifills=dfills=0;
        iwrite_back=dwrite_back=0;
        evicts=devicts=ievicts=0;

        // set
        total_writes = 0;
        total_reads = 0;
        // deads
        total_deads = 0;
        total_alive = 0;
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
    void increase_dead_count(){counter.total_deads++;}
    void increase_alive_count(){counter.total_alive++;}

    IntPtr get_writes(){return counter.total_writes;}
    IntPtr get_reads(){return counter.total_reads;}
    IntPtr get_deads(){return counter.total_deads;}
    IntPtr get_alive(){return counter.total_alive;}
};

class CacheStat{
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

    // map<int, SetStatus> set_status; 
    map<int, SetStatus> set_status;

    // nvm profile
    double avg_write_per_block, avg_write_per_set;
    double inter_set_wv, intra_set_wv;
    int write_latency, read_latency;

    public:
    int fill_bypass, writeback_bypass;
    
    CacheStat(){
        counter = new Counter();
        invalid_writes=invalid_evicts=0;
        invalid_fill=invalid_writeback=0;
        write_latency = read_latency = 0;

        avg_write_per_block=avg_write_per_set=inter_set_wv=intra_set_wv=0;
        fill_bypass=writeback_bypass = 0;
    }

    /*
        exmample, if your catching all writes at LLC then writebacks here implies writes from L2 not the writeback because eviction from LLC those will be evicts.
        1.[ PACKET_TYPE (ipkt=0/dpkt=2) x WRITE_TYPE (fill=0/wb=1) ] => [ ipkt+fill=0 / ipkt+wb=1 / dpkt+fill=2 / dpkt+wb=3 ]
    */
    void increase(WRITE_TYPE type){
        switch(type){
            case WRITE_TYPE::DFILL:
                counter->dfills++;
                counter->dwrites++;
                break;
            case WRITE_TYPE::DWRITEBACK:
                counter->dwrite_back++;
                counter->dwrites++;
                break;
            case WRITE_TYPE::IFILL:
                counter->ifills++;
                counter->iwrites++;
                break;
            case WRITE_TYPE::IWRITEBACK:
                counter->iwrite_back++;
                counter->iwrites++;
                break;
            default: break;
        }
    }

    /*
    1.count evicts from LLC
    2.count dead blocks corresponding to set
    */
    void process_evicts(PACKET pkt){
        PACKET_TYPE pkt_type = pkt.packet_type;
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

    void process_evicted_blocks_life(PACKET& pkt, int set){
        auto index = set_status.find(set);
        if(index==set_status.end())
        {
            set_status.insert({set, SetStatus()});
        }

        switch(pkt.packet_life){
            case PACKET_LIFE::DEAD:
                set_status[set].increase_dead_count();
                break;
            case PACKET_LIFE::ALIVE:
                    set_status[set].increase_alive_count();
                break;
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
    void increase_invalid_inserts(WRITE_TYPE write){
        invalid_writes++;
        switch(write){
            case WRITE_TYPE::FILL:
                invalid_fill++;
                break;
            case WRITE_TYPE::WRITE_BACK:
                invalid_writeback++;
                break;
            default:
                break;
        }
    }

    /*
    1.update write counter on set
    */
    void update_setstatus_on_write(int set){
        auto index = set_status.find(set);
        if(index == set_status.end()){
            set_status.insert({set, SetStatus()});
        }
        index->second.increase_write_count();
    }

    void update_setstatus_on_read(int set){
        auto index = set_status.find(set);
        if(index==set_status.end()){
            set_status.insert({set, SetStatus()});
        }
        index->second.increase_read_count();
    }

    bool is_set_write_intensive(int set){
        double avg = (double)counter->get_total_writes()/(double)set_status.size();
        IntPtr set_writes=0;
        auto index = set_status.find(set);
        if(index!=set_status.end()){
            set_writes = index->second.get_writes();
        }
        if(set_writes > avg) return true; // write intensize set
        return false; //else not
    }

    /*
    1.store for printing: intra, inter, avg_wrt_block, avg_wrt_set
    */
    void store_nvm_profile(double intra, double inter, double avg_block, double avg_set, int wql, int rql){
        avg_write_per_block = avg_block;
        avg_write_per_set = avg_set;
        inter_set_wv = inter;
        intra_set_wv = intra;
        write_latency = wql;
        read_latency = rql;
    }

    double get_total_writes(){
        double total=0;
        for(auto set: set_status){
            total+=set.second.get_writes();
        }
        return total;
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

            f << "Write Characteristics\n";
            uint64_t mX = 0;
            for(auto e: set_status){
                if(e.second.get_writes()>mX){
                    mX=e.second.get_writes();
                }
            }
            f << "avg_wrt_block, avg_wrt_set, max_writes\n";
            f << avg_write_per_block <<","<<avg_write_per_set<<","<<mX<<"\n";
            f << "inter_wv, intra_wv\n";
            f <<inter_set_wv<<","<<intra_set_wv<<'\n';
            f << "fillbypass, writebackbypass\n";
            f << fill_bypass <<","<<writeback_bypass<<'\n';
            f << "WQ Latency, RQ Latency\n";
            f << write_latency << "," << read_latency << '\n';

            f.close();
        }

        // print set stuff
        {
            string s = "llc_set_status.log";
            fstream f = Log::get_file_stream(s);
            
            f << "set,writes,reads,dead\n";
            for(auto set: set_status){
                f << set.first << "," << set.second.get_writes() << "," << set.second.get_reads() <<","<<set.second.get_deads() << "\n"; 
            }
        }
    }
};

#endif