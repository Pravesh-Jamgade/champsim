#ifndef BACKTRACK_LOG_H
#define BACKTRACK_LOG_H

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

extern int KNOB_ENABLE_LOG;
class BacktrackLog
{
private:
    std::vector<std::string> history;
    bool flag = false;

    // internal helper: actually logs and stores
    template<typename... Args>
    void log(const Args&... args)
    {
        std::ostringstream oss;
        // fold expression to print all args separated by space
        ((oss << args << ' '), ...);

        std::string line = oss.str();

        // store in history
        history.push_back(line);
        if (history.size() > KNOB_ENABLE_LOG) {
            history.erase(history.begin()); // simple ring behaviour
        }
    }

public:
    BacktrackLog() = default;
    explicit BacktrackLog(int hist_len) : flag(hist_len > 0) {}

    template<typename... Args>
    void track(const Args&... args)
    {
        if (!flag) return;
        log(args...);
    }

    void print_logs()
    {
        for(auto entry: history)
        {
            std::cout << entry;
        }
    }
};

#endif // BACKTRACK_LOG_H
