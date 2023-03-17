// *** 
#define IntPtr uint64_t

#pragma once
enum PREDICTION{
    NO_PREDICTION=-1,
    DEAD=0,
    ALIVE=1
};

#pragma once
enum LOG{
    A=0,
    B=1
};

#pragma once
enum WRITE{
    FILL=0,
    WRITEBACK,
    IFILL=0,
    IWRITEBACK,
    DFILL=2,
    DWRITEBACK,
};

#pragma once
enum PACKET_TYPE{
    INVALID=-1,
    IPACKET=0,
    DPACKET=2
};

#define EPOC_SCALE 1000000