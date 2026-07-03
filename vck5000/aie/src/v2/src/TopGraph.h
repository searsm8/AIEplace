#pragma once

#include <adf.h>
#include "DensityGraph.h"

class AIEplaceGraph : public adf::graph {
  public:
    DensityGraph density;
};
