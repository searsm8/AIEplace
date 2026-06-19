
#include "PlacementEngine.h"

namespace AIEPLACE_NAMESPACE {

  void VCK5000PlacementEngine::set_data()
  {
    std::cout << "vck5000 set_data\n";
  }
  void VCK5000PlacementEngine::run()
  {
    std::cout << "vck5000driver run\n";
  }

  void CPUPlacementEngine::set_data()
  {
    std::cout << "cpu set_data\n";
  }
  void CPUPlacementEngine::run()
  {
    std::cout << "cpu run\n";
    partials_simple();
  }

  void CPUPlacementEngine::partials_simple()
  {
    for (auto& [netsize, lib] : db_.netLib_) {
      const std::vector<Net>& nets = lib.data();
      for (const Net& net : nets) {

        // find max and min x and y probe positions
        float min_x = __FLT_MAX__, min_y = __FLT_MAX__, max_x = -__FLT_MAX__, max_y = -__FLT_MAX__;
        for (const Net::NetPin& pin : net.netpins) {
          Coordinate pincoord = db_.getNetPinCoordinates(pin);
          min_x = std::min(min_x, pincoord.x);
          min_y = std::min(min_y, pincoord.y);
          max_x = std::max(max_x, pincoord.x);
          max_y = std::max(max_y, pincoord.y);
        }

        for (const Net::NetPin& pin : net.netpins) {
          Coordinate pincoord = db_.getNetPinCoordinates(pin);

          // Compute partials using shortcut
          const int THRESHOLD = 10; // distance threshold for partials
          Gradient simple_partial;
          if (pincoord.x < min_x + THRESHOLD) {
            simple_partial.x = (int(pincoord.x - min_x) * 0.1f) - 1;
          } else if (pincoord.x > max_x - THRESHOLD) {
            simple_partial.x = 1 - (int(max_x - pincoord.x) * 0.1f);
          } else {
            simple_partial.x = 0;
          }

          if (pincoord.y < min_y + THRESHOLD) {
            simple_partial.y = (int(pincoord.y - min_y) * 0.1f) - 1;
          } else if (pincoord.y > max_y - THRESHOLD) {
            simple_partial.y = 1 - (int(max_y - pincoord.y) * 0.1f);
          } else {
            simple_partial.y = 0;
          }

          db_.addGradient(pin, simple_partial);
        }
      }
    }
  }

}
