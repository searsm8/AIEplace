#ifndef SYSTEM_SETTINGS_H
#define SYSTEM_SETTINGS_H

// system_settings.h -- pl_algo AIE common include surface.
// For the HPWL-gradient bring-up this only needs to pull in the ADF + AIE API
// headers used by the kernels. FFT / density_grad settings (POINT_SIZE, twiddle
// types, etc.) will be added here when the density_grad graph is ported.

#include <adf.h>
#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

#endif
