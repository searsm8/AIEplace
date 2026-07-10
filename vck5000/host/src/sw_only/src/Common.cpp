// Common.cpp
// Contains functions defined in Common.h
#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN

// Execution time tracking functions
long getTime() {
    struct timeval tm;
    gettimeofday(&tm, NULL);
    return (tm.tv_sec * 1000000) + tm.tv_usec;
}

double getInterval(long start_time, long end_time) {
    return (end_time - start_time) / 1.0e6;
}

// @brief: return a human-readable thread index, mapped using thread ID
std::size_t get_index(const std::thread::id id)
{
    static std::size_t nextindex = 0;
    static std::mutex my_mutex;
    static std::unordered_map<std::thread::id, std::size_t> ids;
    std::lock_guard<std::mutex> lock(my_mutex);
    auto iter = ids.find(id);
    if(iter == ids.end())
        return ids[id] = nextindex++;
    return iter->second;
}

AIEPLACE_NAMESPACE_END