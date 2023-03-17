#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <fstream>
#include <algorithm>
#include "constant.h"

using namespace std;

class Log{
    public:
    static fstream get_file_stream(string name){
        fstream newfs;
        newfs.open(name.c_str(), std::fstream::app | std::fstream::in);
        return newfs;
    }
    private:
    
};

#endif