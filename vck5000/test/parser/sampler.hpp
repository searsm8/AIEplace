// sampler.hpp -- a tiny in-process sampling profiler for parse_bench (this box has no perf, and
// ptrace_scope=1 forbids attaching gdb). Enabled by SAMPLE_PROFILE=<out file>: a 1 ms
// ITIMER_PROF signal records the call stack; at exit each sample is written as one line of
// frames, innermost first. Frames in the executable are raw addresses (the harness is linked
// -no-pie so addr2line can resolve them); frames in shared libraries are dladdr symbol names.
// Aggregate with sampler_report.sh. Meow.
#pragma once
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/time.h>

namespace sampler {

constexpr int MAX_DEPTH = 24;
constexpr int MAX_SAMPLES = 200000;

struct Sample { int depth; void* frames[MAX_DEPTH]; };
inline Sample* g_samples = nullptr;
inline volatile int g_count = 0;
inline const char* g_out = nullptr;

inline void onSignal(int)
{
    if (g_count >= MAX_SAMPLES) return;
    Sample& s = g_samples[g_count];
    s.depth = backtrace(s.frames, MAX_DEPTH);
    g_count = g_count + 1;
}

inline void write()
{
    if (!g_out) return;
    itimerval off = {};
    setitimer(ITIMER_PROF, &off, nullptr);
    FILE* out = std::fopen(g_out, "w");
    if (!out) return;
    for (int i = 0; i < g_count; i++) {
        // frames[0..1] are the handler and the signal trampoline
        for (int f = 2; f < g_samples[i].depth; f++) {
            void* addr = g_samples[i].frames[f];
            Dl_info info;
            bool in_library = dladdr(addr, &info) && info.dli_fname && !std::strstr(info.dli_fname, "parse_bench");
            if (in_library && info.dli_sname)
                std::fprintf(out, "%s ", info.dli_sname);
            else if (in_library)   // a library-internal symbol, e.g. libc's _int_malloc
                std::fprintf(out, "[%s] ", std::strrchr(info.dli_fname, '/') ? std::strrchr(info.dli_fname, '/') + 1 : info.dli_fname);
            else
                std::fprintf(out, "%p ", addr);
        }
        std::fprintf(out, "\n");
    }
    std::fclose(out);
}

inline void start()
{
    g_out = std::getenv("SAMPLE_PROFILE");
    if (!g_out) return;
    g_samples = (Sample*)std::calloc(MAX_SAMPLES, sizeof(Sample));
    void* warm[4];
    backtrace(warm, 4);   // the first call may allocate; do it outside the handler
    std::signal(SIGPROF, onSignal);
    itimerval timer = {{0, 1000}, {0, 1000}};
    setitimer(ITIMER_PROF, &timer, nullptr);
    std::atexit(write);
}

} // namespace sampler
