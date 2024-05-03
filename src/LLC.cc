#include "LLC.h"
// #include "cache.h"

LLC::LLC()
{
{
    for(int i=0; i< NUM_BANKS; i++)
    {
        banks.push_back(
          CACHE("LLC", LLC_SET, LLC_WAY, LLC_SET*LLC_WAY, LLC_WQ_SIZE, LLC_RQ_SIZE, LLC_PQ_SIZE, LLC_MSHR_SIZE, this)
        );
    }
}
}

int LLC::add_rq(PACKET *packet)
{

}

int LLC::add_wq(PACKET *packet)
{

}

int LLC::add_pq(PACKET* packet)
{

}

void LLC::return_data(PACKET* packet)
{

}

void LLC::operate()
{

}

void LLC::increment_WQ_FULL(uint64_t addr)
{

}

uint32_t LLC::get_occupancy(uint8_t queue_type, uint64_t addr)
{

}

uint32_t LLC::get_size(uint8_t queue_type, uint64_t addr)
{

}


