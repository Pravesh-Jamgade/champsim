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
    int head = 0;

    template<typename... Args>
    void log(const Args&... args)
    {
        std::ostringstream oss;
        ((oss << args << ' '), ...);
        history[head] = oss.str();      // overwrite
        head = (head + 1) % KNOB_ENABLE_LOG;
    }

public:
    BacktrackLog() = default;
    explicit BacktrackLog(int hist_len) : flag(hist_len > 0) {
        history.resize(KNOB_ENABLE_LOG);
        cout << "Log Setting:\nLog Enable, " << flag << '\n';
        cout << "Log History Size, " << history.size() << '\n';
    }

    template<typename... Args>
    void track(const Args&... args)
    {
        if (!flag) return;
        log(args...);
    }

    void print_logs()
    {
        for(int i=head; i< KNOB_ENABLE_LOG; i++)
        {
            cout << history[i];
        }

        for(int i=0; i< head; i++)
        {
            cout << history[i];
        }
    }
};

#endif // BACKTRACK_LOG_H
