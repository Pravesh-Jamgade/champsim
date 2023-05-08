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
enum class WRITE_TYPE{
    FILL=0,
    WRITE_BACK,
    IFILL=0,
    IWRITEBACK,
    DFILL=2,
    DWRITEBACK,
    INVALID=-1
};

#pragma once
enum PACKET_TYPE{
    INVALID=-1,
    IPACKET=0,
    DPACKET=2
};

#pragma once
enum STAT{
    FP=0,
    FN,
    TP,
    TN
};

/*Block found in cache?, Miss=0 Hit=1*/
enum RESULT{
    MISS=0,
    HIT
};

#pragma once
enum class PACKET_LIFE{
    INVALID =-1,
    DEAD,
    ALIVE
};

/***********************************
 * Configuration For 
 * Bypassing
 * EpocSize
************************************/

#define EPOC_SCALE 1000000

// 01 no bypass (default)  1
// 10 bypass aggresive     2
// 11 bypass selective     3
#define SUPER_USER_BYPASS 3
