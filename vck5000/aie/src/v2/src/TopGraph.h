#pragma once

#include <adf.h>
#include "DensityGraph.h"

#define DENSITY_CHANNELS 1

class AIEplaceGraph : public adf::graph {
  public:
    DensityGraph density;
};
