#ifndef PL_ALGO_DENSITY_VERIFY_HPP
#define PL_ALGO_DENSITY_VERIFY_HPP

// DensityVerify.hpp -- verify the PL bin-density module (density_bin) against the
// sw_only Grid golden on a real benchmark. Builds rho via Grid::computeBinOverlaps
// / clampFixedDensity / getBinDensities (fillers EXCLUDED in v1), runs density_bin
// on the device, and compares rel_rms = ||dev - golden|| / ||golden||.

#include "PackedDesign.hpp"

namespace AIEplace { class DataBase; }

namespace plalgo {

// Returns 0 on PASS (rel_rms below tolerance), 1 on FAIL.
int runDensityVerify(AIEplace::DataBase& db, const PackedDesign& pk, const char* xclbin_path);

} // namespace plalgo

#endif // PL_ALGO_DENSITY_VERIFY_HPP
