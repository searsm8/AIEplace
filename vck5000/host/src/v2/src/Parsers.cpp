#include "Parsers.h"

namespace AIEPLACE_NAMESPACE {

  void CellParser::parseFile(const std::string& filename) {
    std::ifstream ifs(filename);
    if (!ifs) {
      throw std::runtime_error("Cannot open LEF file: " + filename);
    }

    resetState();

    std::string line;
    while (std::getline(ifs, line)) {
      processLine(line);
    }

    // Basic sanity: no macro left open
    if (state_ != State::Idle) {
      throw std::runtime_error("LEF parse error: unterminated MACRO or PIN");
    }
  }

  void CellParser::resetState() {
    state_ = State::Idle;
    currentMacroName_.clear();
    currentMacroKind_ = ComponentKind::Unknown;
    currentMacroWidth_ = currentMacroHeight_ = 0.0f;
    currentMacroIndex_ = std::numeric_limits<uint32_t>::max();

    currentPinName_.clear();
    currentPinHasRect_ = false;
    currentPinDx_ = currentPinDy_ = 0.0f;
  }

  void CellParser::trimInPlace(std::string& s) {
    auto notspace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
  }

  std::vector<std::string> CellParser::tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) {
      tokens.push_back(tok);
    }
    return tokens;
  }

  bool CellParser::iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i])))
        return false;
    }
    return true;
  }

  void CellParser::processLine(std::string line) {
    trimInPlace(line);
    if (line.empty() || line[0] == '#')
      return;

    auto tokens = tokenize(line);
    if (tokens.empty())
      return;

    const std::string& kw = tokens[0];

    // Global END LIBRARY
    if (iequals(kw, "END") && tokens.size() >= 2 && iequals(tokens[1], "LIBRARY")) {
      // We just ignore it – parseFile will end naturally
      return;
    }

    switch (state_) {
      case State::Idle:
        handleIdle(tokens);
        break;
      case State::InMacro:
        handleInMacro(tokens);
        break;
      case State::InPin:
        handleInPin(tokens);
        break;
      case State::InPort:
        handleInPort(tokens);
        break;
    }
  }

  // --- State handlers ---

  void CellParser::handleIdle(const std::vector<std::string>& tokens) {
    const std::string& kw = tokens[0];

    if (iequals(kw, "MACRO")) {
      if (tokens.size() < 2)
        throw std::runtime_error("MACRO without name");

      currentMacroName_ = tokens[1];
      currentMacroKind_ = ComponentKind::Unknown;
      currentMacroWidth_ = currentMacroHeight_ = 0.0f;
      currentMacroIndex_ = std::numeric_limits<uint32_t>::max();

      state_ = State::InMacro;
    }
    // else: ignore other stuff at top level
  }

  void CellParser::handleInMacro(const std::vector<std::string>& tokens) {
    const std::string& kw = tokens[0];

    if (iequals(kw, "CLASS")) {
      // CLASS CORE/BLOCK
      if (tokens.size() >= 2) {
        const std::string& cls = tokens[1];
        if (iequals(cls, "CORE"))  currentMacroKind_ = ComponentKind::LogicCell;
        else if (iequals(cls, "BLOCK")) currentMacroKind_ = ComponentKind::Macro;
        else currentMacroKind_ = ComponentKind::Unknown;
      }
    } else if (iequals(kw, "SIZE")) {
      // SIZE X BY Y ;
      if (tokens.size() >= 4 && iequals(tokens[2], "BY")) {
        currentMacroWidth_  = std::stof(tokens[1]);
        currentMacroHeight_ = std::stof(tokens[3]);
      } else {
        throw std::runtime_error("Malformed SIZE line in macro " + currentMacroName_);
      }
    } else if (iequals(kw, "PIN")) {
      // PIN <name>
      if (tokens.size() < 2)
        throw std::runtime_error("PIN without name in macro " + currentMacroName_);

      // On first PIN, we create the macro entry in the library (if not already)
      ensureMacroInLibrary();

      currentPinName_ = tokens[1];
      currentPinHasRect_ = false;
      currentPinDx_ = currentPinDy_ = 0.0f;

      state_ = State::InPin;
    } else if (iequals(kw, "END")) {
      // END <macro_name>
      if (tokens.size() >= 2 && iequals(tokens[1], currentMacroName_)) {
        // finalize macro in library (if no pins were encountered)
        ensureMacroInLibrary();
        state_ = State::Idle;
      }
    }
    // ignore: FOREIGN, ORIGIN, SYMMETRY, SITE, etc.
  }

  void CellParser::handleInPin(const std::vector<std::string>& tokens) {
    const std::string& kw = tokens[0];

    if (iequals(kw, "PORT")) {
      // Begin port region
      state_ = State::InPort;
    } else if (iequals(kw, "END")) {
      // END <pin_name>
      // When we leave a PIN, if we saw at least one RECT center, store it.
      if (tokens.size() >= 2 && iequals(tokens[1], currentPinName_)) {
        if (currentPinHasRect_) {
          addCurrentPinToMacro();
        } else {
          // No geometry found; still store with (0,0) if you want,
          // or skip. Here we skip.
        }
        state_ = State::InMacro;
      }
    }
    // ignore: DIRECTION
  }

  void CellParser::handleInPort(const std::vector<std::string>& tokens) {
    const std::string& kw = tokens[0];

    if (iequals(kw, "END")) {
      // END of PORT
      state_ = State::InPin;
    } else if (iequals(kw, "RECT")) {
      // RECT x1 y1 x2 y2 ;
      if (tokens.size() < 5) {
        throw std::runtime_error("Malformed RECT in pin " + currentPinName_ +
            " of macro " + currentMacroName_);
      }
      float x1 = std::stof(tokens[1]);
      float y1 = std::stof(tokens[2]);
      float x2 = std::stof(tokens[3]);
      float y2 = std::stof(tokens[4]);

      // For simplicity: use the center of the first RECT as pin location
      if (!currentPinHasRect_) {
        currentPinDx_ = 0.5f * (x1 + x2);
        currentPinDy_ = 0.5f * (y1 + y2);
        currentPinHasRect_ = true;
      }
      // If multiple RECTs, you might want a better policy
    }
    // ignore: LAYER, VIA, SHAPE, etc.
  }

  void CellParser::ensureMacroInLibrary() {
    if (currentMacroIndex_ != std::numeric_limits<uint32_t>::max())
      return;

    // Create macro ComponentType in library
    currentMacroIndex_ =
      lib_.emplace(
          currentMacroName_,
          currentMacroKind_,
          currentMacroWidth_,
          currentMacroHeight_
          );
  }

  void CellParser::addCurrentPinToMacro() {
    if (currentMacroIndex_ == std::numeric_limits<uint32_t>::max()) {
      throw std::logic_error("Pin encountered before macro is created in library");
    }
    ComponentType& ct = lib_.at_index(currentMacroIndex_);

    uint32_t pinIdx = ct.numPins();
    ct.pinDx.push_back(currentPinDx_);
    ct.pinDy.push_back(currentPinDy_);
    ct.pinIndex.emplace(currentPinName_, pinIdx);
  }

}
