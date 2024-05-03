#include "cache.h"

//---------------------------------------------DK----------------------------------------//
uint32_t IPV[LLC_WAY+1]={0,0,1,0,3,0,1,2,1,0,5,1,0,0,1,11,13};

// initialize replacement state
void CACHE::llc_initialize_replacement()
{

  
}

// find replacement victim
uint32_t CACHE::llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
   
   // LRU block will be evicted
    uint32_t way = 0;

    // fill invalid line first
    for (way=0; way<LLC_WAY; way++) {
        if (block[set][way].valid == false) {
            break;
        }
    }
    // LRU victim
    if (way == LLC_WAY) {
        for (way=0; way<LLC_WAY; way++) {
            if (block[set][way].lru == LLC_WAY-1) {
                break;
            }
            //cout<<block[set][way].lru<<" ";
        }
    }

    if (way == LLC_WAY) {
        cerr << "[" << NAME << "] " << __func__ << " no victim! set: " << set << endl;
        assert(0);
    }

    return way;
}

// called on every cache hit and cache fill
void CACHE::llc_update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    string TYPE_NAME;
    if (type == LOAD)
        TYPE_NAME = "LOAD";
    else if (type == RFO)
        TYPE_NAME = "RFO";
    else if (type == PREFETCH)
        TYPE_NAME = "PF";
    else if (type == WRITEBACK)
        TYPE_NAME = "WB";
    else
        assert(0);

    if (hit)
        TYPE_NAME += "_HIT";
    else
        TYPE_NAME += "_MISS";

    if ((type == WRITEBACK) && ip)
        assert(0);
        

    // uncomment this line to see the LLC accesses
    // cout << "CPU: " << cpu << "  LLC " << setw(9) << TYPE_NAME << " set: " << setw(5) << set << " way: " << setw(2) << way;
    // cout << hex << " paddr: " << setw(12) << paddr << " ip: " << setw(8) << ip << " victim_addr: " << victim_addr << dec << endl;
    
    if (hit)
       {
            // when hit we have to update the position of referenced block using IPV.

            //Promotion of the block

            uint32_t new_LRUpost=IPV[block[set][way].lru];
            uint32_t present_LRUpost=block[set][way].lru;
            
            if(new_LRUpost<present_LRUpost)
            {
                for(int i=0;i<LLC_WAY;i++)
                {
                    if(new_LRUpost<=block[set][i].lru && block[set][i].lru<present_LRUpost
                     && block[set][i].valid)
                    {
                       block[set][i].lru++;
                    }
                }

            }
            else if(new_LRUpost>present_LRUpost)
            {
                for(int i=0;i<LLC_WAY;i++)
                {
                    if(present_LRUpost<block[set][i].lru && block[set][i].lru<=new_LRUpost && block[set][i].valid)
                    {
                        block[set][i].lru--;
                    }
                }

            }
            block[set][way].lru=new_LRUpost;
       }
    else
    {
        //here, we have to get the insertion position.
        //insertion position in IPV is gotten from the i=k.
        uint32_t insert_LRUpost=IPV[LLC_WAY];
        for(int i=0;i<LLC_WAY;i++)
        {
            if(block[set][i].lru>=insert_LRUpost && block[set][i].lru < LLC_WAY-1)
            {
                block[set][i].lru++;      
            }
        }
        block[set][way].lru=IPV[LLC_WAY];
    }
       
}

// use this function to print out your own stats at the end of simulation
void CACHE::llc_replacement_final_stats()
{

}

//-------------------------------------------DK----------------------------------------//