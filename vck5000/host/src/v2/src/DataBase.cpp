
#include "DataBase.h"
#include "Logger.h"

namespace AIEPLACE_NAMESPACE {

  void DataBase::init(fs::path design_dir) {
#ifdef DEBUG
    TIME_BLOCK("DataBase read input");
    Logger::log_info("Reading design from directory: " + m_input_dir.string());
#endif
    parseDesign(design_dir);
    initializeComponentCoordinates();
    calculateBinDensity();
#ifdef DEBUG
    TIME_BLOCK("DataBase read input end");
    Logger::log_info("Finished reading design from directory: " + m_input_dir.string());
#endif
  }

  void DataBase::parseDesign(fs::path design_dir) {
    if (!fs::exists(design_dir) || !fs::is_directory(design_dir)) {
      throw std::runtime_error("Provided path is not a directory or does not exist");
    }

    fs::path cellsPath;
    fs::path floorplanPath;

    for (const auto& entry : fs::directory_iterator(design_dir)) {
      if (!fs::is_regular_file(entry))
        continue;

      const auto& p = entry.path();
      if (p.filename() == "cells.lef") {
        cellsPath = p;
      } else if (p.filename() == "floorplan.def") {
        floorplanPath = p;
      }

      if (!cellsPath.empty() && !floorplanPath.empty())
        break;
    }

    CellParser cellparser(typeLib_);
    FloorplanParser floorplanparser(componentLib_, typeLib_, netLib_, floorplanPath);

    floorplanparser.parseFileMetadata();
    die_.setDimensions(floorplanparser.dieWidth(), floorplanparser.dieHeight(), bins_per_row_, bins_per_col_);

    cellparser.parseFile(cellsPath);
    floorplanparser.parseIOPads();
    addFillerType();
    typeLib_.sort(ComponentTypeComparator{});
    typeLib_.lock();

    floorplanparser.parseComponents();
    addFillers(0.9f);
    componentLib_.sort(ComponentComparator{});
    componentLib_.lock();

    // Setup size of gradients vector
    gradients_.resize(componentLib_.size());

    floorplanparser.parseNets();

    for (auto& [netsize, nets] : netLib_) {
      nets.sort(NetComparator{});
      nets.lock();
    }

    // Get Die size
  }

  void DataBase::addFillerType() {
    if (typeLib_.empty()) {
      throw std::runtime_error("The database does not yet contain any ComponentType. Can't add FillerType.");
    }
    uint32_t dummyIndex;
    if (typeLib_.find_index("Filler", dummyIndex)) {
      throw std::runtime_error("typeLib_ already has a Filler type");
    }
    // Use the average area of ComponentType of ComponentKind::LogicCell in the typeLib_as the size for the Filler
    float total_logic_cell_area = 0.0f;
    uint32_t num_logic_cells = 0;
    for (ComponentType& ct : typeLib_) {
      if (ct.kind == ComponentKind::LogicCell) {
        total_logic_cell_area += ct.box.getArea();
        num_logic_cells++;
      }
    }
    float avg_logic_cell_area = total_logic_cell_area / (float)num_logic_cells;
    typeLib_.emplace("Filler",ComponentKind::Filler, std::sqrt(avg_logic_cell_area), std::sqrt(avg_logic_cell_area));
  }

  void DataBase::addFillers(float target_density) {
    if (target_density < 0.0f || target_density > 1.0f) {
      throw std::runtime_error("addFillers(): target_density should be a value between [0,1]. Got: " + std::to_string(target_density));
    }
    uint32_t fillerTypeIndex;
    if (! typeLib_.find_index("Filler", fillerTypeIndex)) {
      throw std::runtime_error("typeLib_ doesn't have a Filler type");
    }

    double total_component_area = 0.0f;
    for(auto& component : componentLib_) {
      total_component_area += typeLib_[component.typeIndex].box.getArea();
    }
    float area_to_be_filled = (die_.getDieArea() * target_density) - total_component_area;

    const ComponentType& fillerType = typeLib_[fillerTypeIndex];
    int32_t num_fillers_needed = area_to_be_filled / fillerType.box.getArea();
    for (int32_t i = 0; i < num_fillers_needed; i++) {
      componentLib_.emplace("filler"+std::to_string(i), fillerTypeIndex);
    }
  }

  void DataBase::initializeComponentCoordinates() {
    // Choose a random position based on parameters
    // TODO: Different initial position "shapes" could help with performance?
    // e.g. maybe a donut shape would be good.
    Coordinate die_center(die_.getDieWidth()/2.0f, die_.getDieHeight()/2.0f);

    int min_dist = 0;
    int max_dist = die_.getDieWidth()/4.0f;

    for (Component& component : componentLib_) {
      if (component.state == ComponentState::Unplaced) {
        int x_offset = min_dist + rand()%(max_dist-min_dist);
        if(rand()%2 == 1) x_offset *= -1;
        int y_offset = min_dist + rand()%(max_dist-min_dist);
        if(rand()%2 == 1) y_offset *= -1;
        component.origin = die_center + Coordinate(x_offset, y_offset);
      }
    }
  }

  void DataBase::calculateBinDensity() {
    for (auto& component : componentLib_) {
      auto& ct = typeLib_[component.typeIndex];
      die_.addComponentAreaToDensityBins(ct.box + component.origin);
    }
  }

  Coordinate DataBase::getNetPinCoordinates(const Net::NetPin p) const
  {
    const Component& c = componentLib_[p.component_idx];
    const ComponentType::ComponentPin& cp = typeLib_[c.typeIndex].pins[p.pin_idx];
    return Coordinate(c.origin.x + cp.Dx, c.origin.y + cp.Dy);
  }

  void DataBase::addGradient(Net::NetPin p, Gradient g)
  {
    gradients_[p.component_idx] += g;
  }
  void DataBase::resetGradients()
  {
    for (auto& gradient : gradients_) {
      gradient.clear();
    }
  }
  void DataBase::printInfo() const
  {
    print_component_type_library(typeLib_);
    print_component_library(componentLib_);
    print_net_library(netLib_);
  }
}
