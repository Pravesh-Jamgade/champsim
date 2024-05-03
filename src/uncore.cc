#include "uncore.h"
#include "LLC.h"

// uncore
UNCORE uncore;

// constructor
UNCORE::UNCORE() {
    llc = new LLC;
}

