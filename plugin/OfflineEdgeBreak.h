#ifndef OFFLINEEDGEBREAK_H
#define OFFLINEEDGEBREAK_H

#include <bits/stdc++.h>
#include "log.h"
#define IntPtr uint64_t

using namespace std;

class OfflineEdgeBreak
{
    public:

    bool found_test_csv;
    map<string, set<int>> src_index, dst_index;
    map<string, vector<int>> block_hash_table;
    vector<int> count_broken_edges;
    map<int, pair<string, string>> index_to_edge;

    OfflineEdgeBreak()
    {
        string filePath = "./chain/test.csv";
        FILE* in = freopen(filePath.c_str(), "r", stdin);

        if(!allow())
        {
            found_test_csv = false;
            printf("[Warning] test.csv stage 2!\n");
        }
        else
        {
            found_test_csv = true;
            int i = 0;
            
            string rawInput;
            while( getline( cin, rawInput) )
            {
                vector<string> all_val;

                rawInput = ltrim(rawInput);
                rawInput = rtrim(rawInput);

                int pos;
                while((pos = rawInput.find(" ")) != string::npos)
                {
                    string sub = rawInput.substr(0, pos);
                    rawInput = rawInput.substr(pos+1, rawInput.size());
                    rawInput = ltrim(rawInput);
                    rawInput = rtrim(rawInput);
                    all_val.push_back(sub);
                    // cout << '.' << sub << '.';
                }
                // cout << '.' << rawInput << '.' << '\n';
                all_val.push_back(rawInput);

                src_index[all_val[0]].insert(i);
                dst_index[all_val[1]].insert(i);

                index_to_edge.insert({i, {all_val[0], all_val[1]}});

                cout << all_val[0] << "," << all_val[1] << "," << all_val[2] << '\n';
                i++;
            }

            count_broken_edges.resize(300);

            cout << "size = " << src_index.size() << "," << dst_index.size() << '\n';
        }
    }

    inline std::string& rtrim(std::string& s)
    {
        s.erase(s.find_last_not_of(" ") + 1);
        return s;
    }

    // trim from beginning of string (left)
    inline std::string& ltrim(std::string& s)
    {
        s.erase(0, s.find_first_not_of(" "));
        return s;
    }


    ~OfflineEdgeBreak()
    {
    }

    bool allow()
    {
        return STAGE==2;
    }

    template <typename I> std::string n2hexstr(I w, size_t hex_len = sizeof(I)<<1) {
        static const char* digits = "0123456789ABCDEF";
        std::string rc(hex_len,'0');
        for (size_t i=0, j=(hex_len-1)*4 ; i<hex_len; ++i,j-=4)
            rc[i] = digits[(w>>j) & 0x0f];
        return rc;
    }

    void func_check_edge(IntPtr key, uint32_t block_hash_key)
    {
        string block_hash = to_string(block_hash_key);
        string pc = to_string(key);
        if(!allow()) return;

        //check if pc is part of src of edge
        auto found_src = src_index.find(pc);

        //check if pc is part of dst of edge
        auto found_dst = dst_index.find(pc);

        //pc not on either end of edge
        if(found_dst == dst_index.end() && found_src == src_index.end())
        {
            return;
        }        

        // check if block_hash is seen before

        // not seen
        auto foundBlockHash = block_hash_table.find(block_hash);

        // block was brought by someone
        if(foundBlockHash != block_hash_table.end())
        {
            // if seen before

            // such dst is not amongst top edges, BUT IT COULD BE FROM SRC OF STRONG EDGES
            if(found_dst == dst_index.end())
            {
                // cout << "[DST] ABSENT\n";
                // because block is now again brought by dst node which is not 
                // part of strong edge, we clear such block_hash

                block_hash_table[block_hash].clear();
                // track it if it is among the src of strong edges.
            }
            else
            {
                // cout << "[EDGE] SEARCH\n";
                bool count = false;
                int index = -1;
                // there could be a dst pc part of multiple edges, so check for each of them with their corresponding index
                for(auto dst : found_dst->second)
                {
                    // multiple dst, hence multiple src. check if src.index match with dst.index
                    for(auto src: block_hash_table[block_hash])
                    {
                        if(src == dst)
                        {
                            count = true;
                            index = src;
                            break;
                        }
                    }
                }

                if(count)
                {
                    count_broken_edges[index]++;

                    // reset
                    block_hash_table.erase(foundBlockHash);
                }
            }
        }

        // THIS ALSO TRACK DST as SRC IF DST IS NOT AMONGST STRONG EDGES
       
        // such src is not amongst top edges
        if(found_src == src_index.end())
        {
            // cout << "[SRC] ABSENT\n";
            return;
        }

        // cout << "[SRC] -----------------\n";
        for(auto index : found_src->second)
        {
            block_hash_table[block_hash].push_back(index);
        }
        
    }

    void print()
    {
        string s = "edge_break.log";
        fstream out = Log::get_file_stream(s);

        out << "src, dst, broken\n";
        for(auto entry: index_to_edge)
        {
            out << std::dec << entry.second.first << ',' << entry.second.second << ',' << count_broken_edges[entry.first] << '\n';
        }

        out.close();
    }


};

#endif