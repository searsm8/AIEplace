#ifndef SYSTEM_SETTINGS_H
#define SYSTEM_SETTINGS_H

#include <adf.h>
#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

#define POINT_SIZE 32// Must be a power of two for FFT (min 16)
#define FFT_DATA_TYPE cfloat // cfloat, cint32, or cint16
#define TWIDDLE_TYPE cfloat // cfloat, or cint16
#define NUM_ITER POINT_SIZE

// ---EACH LINE OF IO IS ALWAYS 32b---
// if cfloat or cint32 is used, then input format must be:
// real_val_1 (32b)
// imag_val_1 (32b)
// real_val_2 (32b)
// imag_val_2 (32b)
//
// if cint16 is used, then input format is:
// real_val_1 imag_val_1 (32b)
// real_val_2 imag_val_2
// real_val_3 imag_val_3
//


#define PI 3.1415926535 // TODO: use constants header

#endif