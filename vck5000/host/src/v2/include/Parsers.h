#pragma once

#include "Common.h"
#include "Component.h"
#include "Library.h"

namespace AIEPLACE_NAMESPACE {

  class ParserUtils {
    public:
      // Trim helpers
      void trimInPlace(std::string& s);
      std::vector<std::string> tokenize(const std::string& line);
      bool iequals(const std::string& a, const std::string& b);
      void processLine(std::string line);
      void dropComments(std::string& s);

  };

  class CellParser {
    public:
      explicit CellParser(ComponentTypeLibrary& lib) : lib_(lib) {}

      // Parse a cells.lef file and populate lib_
      void parseFile(const std::string& filename);

    private:
      ComponentTypeLibrary& lib_;
      ParserUtils parserUtils_;

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
      void processLine(std::string line);
      // --- State handlers ---
      void handleIdle(const std::vector<std::string>& tokens);
      void handleInMacro(const std::vector<std::string>& tokens);
      void handleInPin(const std::vector<std::string>& tokens);
      void handleInPort(const std::vector<std::string>& tokens);

      void ensureMacroInLibrary();
      void addCurrentPinToMacro();
  };

  class FloorplanParser {
    public:
      FloorplanParser(ComponentLibrary& compLib, ComponentTypeLibrary& typeLib, std::string filename) : compLib_(compLib), typeLib_(typeLib), ifs_(filename)
    {
      if (!ifs_) {
        throw std::runtime_error("Cannot open DEF file: " + filename);
      }
    }

      // After parseFile you can query these:
      Coordinate dieLowerLeft() const { return metadata_.die_ll; }
      Coordinate dieUpperRight() const { return metadata_.die_ur; }
      float dieWidth() const { return metadata_.die_ur.x - metadata_.die_ll.x; }
      float dieHeight() const { return metadata_.die_ur.y - metadata_.die_ll.y; }

      float dbUnitsPerMicron() const { return metadata_.dbu_per_micron; }

      struct SectionPos {
        std::streampos beginLinePos{}; // position at start of line with "<NAME> <count> ;"
        long count = -1;               // as read from header (e.g. COMPONENTS 108292 ;)
        bool present = false;
      };

      struct FloorplanSectionMap {
        SectionPos components;
        SectionPos iopads;
        SectionPos nets;
        SectionPos vias;
        SectionPos nonDefaultRules;
        SectionPos regions;
        SectionPos groups;
      };

      struct FloorplanMetadata {
        std::string version;
        std::string dividerChar;
        std::string busBitChars;
        std::string designName;
        float dbu_per_micron = 1.0f;
        Coordinate die_ll{0.0f, 0.0f};
        Coordinate die_ur{0.0f, 0.0f};
        // add ROWS, TRACKS here if needed
      };

      void parseFileMetadata();
      void parseComponents();
      void parseIOPads();

    private:

      std::ifstream ifs_;
      ParserUtils parserUtils_;

      ComponentTypeLibrary& typeLib_;
      ComponentLibrary&     compLib_;

      FloorplanMetadata   metadata_;
      FloorplanSectionMap sections_;

      void parseSection(const std::string& name, const SectionPos& section, std::function<void(const std::vector<std::string>&)> lineParser);
      void parseComponentLine(const std::vector<std::string>& tokens);
      void parseIOPadLine(const std::vector<std::string>& tokens);
      void finalizeCurrentComponent();

      // Helper to interpret section header line, e.g. "COMPONENTS 108292 ;"
      void recordSectionHeader(const std::string& line, std::streampos linePos, const std::vector<std::string>& toks, SectionPos& section);
      std::string toUpper(std::string str);
  };

}
