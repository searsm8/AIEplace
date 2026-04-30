#pragma once

#include "Common.h"
#include "Component.h"
#include "Library.h"

namespace AIEPLACE_NAMESPACE {

  class CellParser {
    public:
      explicit CellParser(ComponentTypeLibrary& lib) : lib_(lib) {}

      // Parse a cells.lef file and populate lib_
      void parseFile(const std::string& filename);

    private:
      ComponentTypeLibrary& lib_;

      enum class State {
        Idle,
        InMacro,
        InPin,
        InPort
      };
      State state_ = State::Idle;

      // current macro being built
      std::string currentMacroName_;
      ComponentKind currentMacroKind_ = ComponentKind::Unknown;
      float currentMacroWidth_  = 0.0f;
      float currentMacroHeight_ = 0.0f;
      uint32_t currentMacroIndex_ = std::numeric_limits<uint32_t>::max();

      // current pin
      std::string currentPinName_;
      bool        currentPinHasRect_ = false;
      float       currentPinDx_ = 0.0f;
      float       currentPinDy_ = 0.0f;

      void resetState();

      // Trim helpers
      void trimInPlace(std::string& s);
      std::vector<std::string> tokenize(const std::string& line);
      bool iequals(const std::string& a, const std::string& b);
      void processLine(std::string line);

      // --- State handlers ---
      void handleIdle(const std::vector<std::string>& tokens);
      void handleInMacro(const std::vector<std::string>& tokens);
      void handleInPin(const std::vector<std::string>& tokens);
      void handleInPort(const std::vector<std::string>& tokens);

      void ensureMacroInLibrary();
      void addCurrentPinToMacro();
  };

}
