#ifndef INFO
#define INFO

#include <bits/stdc++.h>
#include "log.h"
using namespace std;

#define IntPtr uint64_t

class Meta
{
    public:
    IntPtr write_count;
    map<IntPtr, IntPtr> write_type;
    // cummulative count of writes of pc, at an cycle number
    vector<IntPtr> ccw;

    // collection of bag of follow-up PC
    vector<vector<IntPtr>> corpus_of_pc;

    // duplicates in corpus
    int duplicate = 0;

    // curr bag
    int ptr = 0;

    void add_writes(){
        ccw.push_back(write_count);
    }

    void add_followup_pc(vector<IntPtr> all_pc)
    {
        for(auto vec: corpus_of_pc)
        {
            if( ! std::equal(vec.begin(), vec.end(), all_pc.begin(), all_pc.end()))
            {
                corpus_of_pc.push_back(all_pc);
            }
            else duplicate++;
        }
    }

    void add_type_write(int type)
    {
        if(write_type.find(type)==write_type.end())
        {    
            write_type[type] = 1;
        }
        write_type[type]++;
    }
};

class Info
{
    public:

    Info(){}
    ~Info(){}

    map<IntPtr, Meta> pc_write;
    map<IntPtr, Meta> page_write;

    /*key -> pc/page and what -> 0/1 */
    void func_add_write(IntPtr key, bool what, int packet_type)
    {
        if(what == 0)
        {
            if(pc_write.find(key)==pc_write.end()){ pc_write[key].write_count = 1;}
            else pc_write[key].write_count++;

            pc_write[key].add_type_write(packet_type);
        }
        else
        {
            if(page_write.find(key)==page_write.end()){ page_write[key].write_count = 1;}
            else page_write[key].write_count++;

            page_write[key].add_type_write(packet_type);
        }
    }

    /*track cummulative writes at the end of epoc*/
    void func_track_write(IntPtr cycle)
    {
        for(auto e: pc_write)
        {
            e.second.add_writes();
        }

        for(auto e: page_write)
        {
            e.second.add_writes();
        }
    }

    /*chain of PC occured on cache block before eviction*/
    void func_track_bag_of_pc(vector<IntPtr> bag_pc)
    {
        if(bag_pc.size()<=1) 
        {
            return;
        }
        pc_write[bag_pc[0]].add_followup_pc(bag_pc);
    }

    /*print all karma*/
    void func_print()
    {
        string s = "pc_write.log";
        fstream a = Log::get_file_stream(s);

        s = "pc_writeType.log";
        fstream c = Log::get_file_stream(s);

        s = "pc_stat.log";
        fstream b = Log::get_file_stream(s);

        s = "pc_writeCurve.log";
        fstream d = Log::get_file_stream(s);

        for(auto e: pc_write)
        {
            a << e.first << "," << e.second.write_count << '\n';
            for(auto f: e.second.write_type)
            {
                c << e.first << "," << f.first << "," << f.second << '\n';
            }

            d << e.first << ",";
            for(auto g: e.second.ccw)
            {
                d << g << ",";
            }
            d << '\n';
        }

        b << "unique pc," << pc_write.size() << '\n';

        a.close(), c.close(), b.close(), d.close();

        // *************************PPP****AAA*****GGG***EEE******************************************* //

        s = "page_write.log";
        a = Log::get_file_stream(s);

        s = "page_writeType.log";
        c = Log::get_file_stream(s);
        
        s = "page_stat.log";
        b = Log::get_file_stream(s);

        s = "page_writeCurve.log";
        d = Log::get_file_stream(s);

        for(auto e: page_write)
        {
            a << e.first << "," << e.second.write_count << '\n';
            for(auto f: e.second.write_type)
            {
                c << e.first << "," << f.first << "," << f.second << '\n';
            }

            d << e.first << ",";
            for(auto g: e.second.ccw)
            {
                d << g << ",";
            }
            d << '\n';
        }

        b << "unique page," << page_write.size() << '\n';


        // pc chain
        s = "pc_chain.log";
        fstream q = Log::get_file_stream(s);
        
        for(auto pc: pc_write)
        {
            for(auto chain: pc.second.corpus_of_pc)
            {
                q << "for:" << pc.first << ',';
                for(auto ele: chain)
                {
                    q << ele << ',';
                }
                q << '\n';
            }
        }
        q.close();
    }


};
#endif