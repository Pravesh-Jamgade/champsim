#ifndef LOGGER_H
#define LOGGER_H

#include<iostream>

using namespace std;

class logger
{
    public:
        logger(){this->flag=true;}
        logger(bool tap);
        virtual ~logger(){

        }
        void log();
        
        template<typename P, typename ...Param>
        void log(const P &p, const Param& ...param);
        
    protected:

    private:
        bool flag=false;
};


template<typename P, typename ...Param>
void logger::log(const P &p, const Param& ...param){
    if(this->flag){
        cout<<p<<" ";
        logger::log(param...);
    }
}

#endif // LOGGER_H