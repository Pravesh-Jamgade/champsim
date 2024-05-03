#include "uncore.h"

// uncore
UNCORE uncore;

// constructor
UNCORE::UNCORE() {
    llc = new LLC;
}

