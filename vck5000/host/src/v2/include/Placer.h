#ifndef AIEPLACE_H
#define AIEPLACE_H

#include "Common.h"
#include "DataBase.h"
#include "PlacementEngine.h"
#include "Logger.h" // or DebugFramework

#include "json.h"
#include "JsonUtils.h"
using json = nlohmann::json;

#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cassert>

namespace AIEPLACE_NAMESPACE {

  struct HyperParameters {
    std::string config_filepath;

    float step_length; // α (alpha) in eplace
    float density_weight; // λ (lambda) in eplace
    float nesterov_ak = 1.0f; // a_k in Algorithm 1; controls momentum coefficient
    float momentum_coeff; // (a_k - 1) / a_{k+1} in Algorithm 1; computed each iteration if momentum enabled
    float gamma, inv_gamma; // smoothness factor for estimations;
                            // larger means less smooth but more accurate

    int warmup_iterations;   // iterations before BB step estimation kicks in
    int backtrack_steps = 0;
    int max_backtracking_attempts;
    float backtrack_epsilon;
    bool enable_backtracking;
    bool enable_momentum;

    int min_die_dimension; // minimum of width and height of the die area
    int bins_per_row; // grid size x-dimension
    int bins_per_column; // grid size y-dimension
    int MAX_THREADS; // max number of threads to use

    // Methods of computation, loaded from config file
    std::string partials_method;
    std::string density_method;

    // Convergence Criteria, loaded from config file
    int min_iterations;
    int max_iterations;
    float hpwl_improvement_threshold;
    float overflow_threshold;
    int convergence_window;
    float target_density;
    bool enable_filler;

    // Configuration object
    fs::path input_dir;
    fs::path output_dir;
    fs::path results_dir;

    std::string xclbin_file;
    bool create_vizualisation;

  };

  class ConfigParser {
    public:
      ConfigParser() {};
      void parse(const std::string& config_filepath, HyperParameters& params);
    private:
      void readconfig(const std::string config_filepath);
      json cfg;
  };

  class Placer
  {
    private:
      float m_initial_hpwl = 0.0f;
      std::string m_config_filepath;
      DataBase& db_;
      HyperParameters& params;
      std::unique_ptr<IPlacementEngine> placement_engine;

      // Helper functions for DSE integration and output organization
      void createRunOutputStructure();
      void writeResultsCSV(float final_hpwl, float final_overflow,
          float total_runtime, float iteration_avg,
          float hpwl_improvement, const std::string& run_id);

      // Helper functions
      std::string escapeJsonString(const std::string& input);
      std::string generateRunId();
      bool checkConvergence();
      float getMemoryUsageMB();
      float getAIEUtilizationPercent();

      // Execution tracking
      int iteration = 0;
      long double pgrm_start_time;
      long double db_IO_time;
      double algo_time = 0.0;
      std::vector<float> hpwl_history; // history of HPWL values for each iteration
      std::vector<float> ovfw_history; // history of overflow values for each iteration
      std::vector<float> step_length_history;

    public:
      Placer(HyperParameters& params, DataBase& db);

      void printWelcomeBanner(bool show_info = true);

      // Pre-run preparation -> TODO
      void initializePlacement(Position target_pos, int min_dist, int max_dist);
      void recordInitialHPWL();
      void iterationReset();
      void initializeDensityWeight();

      // Functions to be accelerated on AIEs -> Move to VCK5000PlacementEngine
      void prepareInputDataPacket(float * input_data, int net_size);
      void computeAllPartials_AIE();
      void computePartials(); 
      void receivePartials();
      void computeElectricFields_AIE();

      // Functions implemented on CPU -> TODO Move to CPUPlacementEngine
      void computeHpwlPartials();
      void computeHpwlPartials_CPU();
      void computeHpwlPartials_simple();
      void computeElectricFields();
      void computeElectricFields_CPU();
      void computeElectricFields_DCT();
      void normalizeElectricFields();
      //Gradient computeElectrostaticForce(Node* node_p);
      void compute_a_uv_naive();
      void compute_eField_naive();
      void compute_a_uv_DCT();
      void compute_eField_DCT();
      void computeOverlaps();

      // Comparison functions for verification -> TODO
      void compareDensityResults();

      // Main algorithm loop functions -> TODO
      void run();
      void performIteration(); //TODO -> move to IPlacementEngine

      // Main algorithm iteration functions -> Move to PlacementEngine
      void computeAllProbeGradients();    // ∇HPWL and ∇D at probe positions; cached on nodes
      void combineGradients();            // subtract electro from probe_grad in-place
      float computeLipshitzEstimate();    // BB step estimate: ||Δv|| / ||Δ∇f||
      void performNextStep(bool backtracking_enabled = true); // Algorithm 2: BkTrk
      void advanceIterationState();       // promote next → current for all nodes
      void stepAllNodes();                // Algorithm 1, lines 2–4
      //void enforceDieBoundaries(Node* node_p);           // clamp next.node_pos to die area
      void updateDensityWeight();

      // Diagnostics
      void logStepDiagnostics();

      // Bookkeeping and visualization
      void recordIterationResults();
      void plotHistories();

      // Post run analysis
      void computeStatistics();

      // Print functions
      void printIterationResults();
      void printFinalResults();
      void initializeFocus();
  };

}

#endif
