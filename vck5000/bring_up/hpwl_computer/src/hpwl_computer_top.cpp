// hpwl_computer_top.cpp -- kernel top wrapping hpwl_computer.
//
// Thin wrapper delegating to the module's own hpwl_computer(): resolve_beat, the comparator
// trees and select_lane_hpwl all live in modules/hpwl_computer.hpp. No interface pragmas yet.

#include "modules/hpwl_computer.hpp"

using namespace plalgo;

extern "C" void hpwl_computer_top(
        const int*  net_count,
        const int*  beat_count,
        const InBeat* pin_beats,
        OutBeat*    out_beats,
        int         num_beats) {

    hpwl_computer(net_count, beat_count, pin_beats, out_beats, num_beats);
}
