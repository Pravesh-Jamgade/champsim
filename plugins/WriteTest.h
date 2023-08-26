#include "../inc/log.h"
#include <bits/stdc++.h>

#define MAX_BUCKET 10000

#define IntPtr uint64_t

using namespace std;

/*bucket name (which is count) and its corresponding frequency*/
class Bucket
{
    class MetaData
    {
        public:
        /*burst_len --> frequency*/
        map<IntPtr, IntPtr> count_plus_freq;
        IntPtr total_writes_on_these_set;

        MetaData(){}
        MetaData(IntPtr count){
            count_plus_freq[count] = 1;
            total_writes_on_these_set = 0;
        }
        
        void insert_count(IntPtr count)
        {
            if(count_plus_freq.find(count)!=count_plus_freq.end())
            {
                count_plus_freq[count]++;
            }
            else
            {
                count_plus_freq[count] = 1;
            }
        }
    };

    public:
   
    Bucket(){}
    
    ~Bucket(){}

    /*set --> metadata */
    map<uint32_t, MetaData> bucket_counter;    

    void insert_bucket(uint32_t set, IntPtr count)
    {
        if(bucket_counter.find(set)!=bucket_counter.end())
        {
            bucket_counter[set].insert_count(count);
        }
        else{
            bucket_counter.insert({set, MetaData(count)});
        }
    }
};

/*identify types of write burst*/
class WriteTest
{
    public:
    vector<IntPtr> *history;
    uint32_t num_way;
    Bucket* bucket_ptr;

    /*param: sets, ways*/
    WriteTest(uint32_t num_set, uint32_t num_way)
    {
        history = new vector<IntPtr>[num_set];
        this->num_way = num_way;
        bucket_ptr = new Bucket();
    }
    ~WriteTest(){}

    bool push(uint32_t set, IntPtr tag)
    {
        if(history[set].size() >= num_way) return false;
        history[set].push_back(tag);
        return true;
    }

    void pop(uint32_t set)
    {
        if(history[set].empty()) return;
        history[set].erase(history[set].begin());
    }

    bool func_find_and_merge(uint32_t set, IntPtr tag)
    {
        if(history[set].empty()) return false;
        if(history[set].back() == tag)
        {
            return true;
        }
        return false;
    }

    void func_cache_block_evicted(uint32_t set, uint32_t burst_count)
    {
        // bucket_ptr[set] = burst_count;
        bucket_ptr->insert_bucket(set, burst_count);
    }

    /*returns true if it is a consecutive burst*/
    bool func_cache_block_hit(uint32_t set, IntPtr tag)
    {
        //not consecutive
        bool found = func_find_and_merge(set, tag);
        if(!found)
        {   
            // not enough space
            if(!push(set, tag))
            {
                pop(set);
                // try again
                push(set, tag);
            }
        }
        return found;
    }

    void func_set_write(uint32_t set, IntPtr total_write)
    {
        bucket_ptr->bucket_counter[set].total_writes_on_these_set = total_write;
    }

    void func_print()
    {
        string s = "llc_write_pattern.log";
        fstream f = Log::get_file_stream(s);

        f << "set, burst_length, freq, writes_per_set\n";
        /*set-->metadata*/
        for(auto set: bucket_ptr->bucket_counter)
        {
            /*burst_len --> freq*/
            for(auto meta: set.second.count_plus_freq)
            {   
                //   set,                burst_length,        freq,                 writes_per_set
                f << set.first << "," << meta.first << "," << meta.second << "," << set.second.total_writes_on_these_set << '\n';
            }
        }
    }
};