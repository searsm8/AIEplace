#ifndef PL_TIER1_STUB_HPP
#define PL_TIER1_STUB_HPP

// tier1_stub.hpp -- lets a pure-g++ tier-1 harness #include the real pl_algo modules whose
// dependency chain reaches formats.hpp's HLS transport headers (ap_int.h / hls_stream.h /
// ap_axi_sdata.h). Those headers do not exist outside Vitis, so this defines PL_TIER1_STUB --
// which formats.hpp reads to guard the HLS includes and the axis_t/beat_t typedefs out -- and
// supplies trivial stand-ins for the only HLS surface a module's *host-facing* signature exposes.
//
// Today that surface is exactly hls::stream<T> (iteration_update's v_{k+1} channel). A FIFO
// backed by std::deque reproduces its blocking read/write semantics for a single-threaded
// harness: the producer writes the whole stream, then the consumer reads it. axis_t / beat_t are
// NOT provided -- no tier-1 module references them; a harness that ever needs one adds it here.
//
// Include this BEFORE any module header, so the #define lands before formats.hpp is parsed. Meow.

#define PL_TIER1_STUB

#include <deque>

namespace hls {

template <class T>
class stream {
    std::deque<T> fifo_;
public:
    void write(const T& v) { fifo_.push_back(v); }
    T    read()            { T v = fifo_.front(); fifo_.pop_front(); return v; }
    bool empty() const     { return fifo_.empty(); }
    int  size()  const     { return (int)fifo_.size(); }
};

} // namespace hls

#endif // PL_TIER1_STUB_HPP
