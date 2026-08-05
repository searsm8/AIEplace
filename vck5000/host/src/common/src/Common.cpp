/**
 * @file Common.cpp
 * @brief Implementations of the free helper functions declared in Common.h (timing).
 */
#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN

// See the contract in Common.h. Overwritten once from params.deterministic during setup.
bool g_deterministic = true;

// Execution time tracking functions
long getTime() {
    struct timeval tm;
    gettimeofday(&tm, NULL);
    return (tm.tv_sec * 1000000) + tm.tv_usec;
}

double getInterval(long start_time, long end_time) {
    return (end_time - start_time) / 1.0e6;
}

AIEPLACE_NAMESPACE_END