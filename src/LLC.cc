#include "LLC.h"
#include "cache.h"

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