#include "logger.h"
logger::logger(bool flag){
    this->flag = flag;
}

void logger::log(){
    cout<<"";
}

// template<typename P, typename ...Param>
// void logger::log(const P &p, const Param& ...param){
//     if(this->flag){
//         cout<<p<<" ";
//         logger::log(param...);
//     }
// }
