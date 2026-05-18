#include "Parsers.h"

namespace AIEPLACE_NAMESPACE {

  // ParserUtils
  //
  std::string FloorplanParser::toUpper(std::string str)
  {
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) {
        return std::toupper(c);
        });

    return str;
  }

  [[noreturn]] void throwMalformedStatement(std::string section, const std::vector<std::string>& toks, const std::string& reason) {
    std::ostringstream oss;
    oss << "Malformed " << section << ": " << reason << "\nTokens:";
    for (const auto& t : toks) {
      oss << " [" << t << "]";
    }
    throw std::runtime_error(oss.str());
  }


  void ParserUtils::trimInPlace(std::string& s) {
    auto notspace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
  }

  std::vector<std::string> ParserUtils::tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;

    auto flush = [&]() {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    };

    for (char c : line) {
      if (std::isspace(static_cast<unsigned char>(c))) {
        flush();
      } else if (c == '(' || c == ')' || c == ',' || c == ';' || c == '+' ) {
        flush();
        std::string s(1, c);
        tokens.push_back(s);
      } else {
        current.push_back(c);
      }
    }
    flush();
    return tokens;
  }

  bool ParserUtils::iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i])))
        return false;
    }
    return true;
  }

  void ParserUtils::dropComments(std::string& line) {
    auto pos = line.find('#');
    if (pos != std::string::npos) {
      line.erase(pos);
    }
  }

  // CellParser

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

  void CellParser::processLine(std::string line) {
    parserUtils_.dropComments(line);
    parserUtils_.trimInPlace(line);

    if (line.empty())
      return;

    auto tokens = parserUtils_.tokenize(line);
    if (tokens.empty())
      return;

    const std::string& kw = tokens[0];

    // Global END LIBRARY
    if (parserUtils_.iequals(kw, "END") && tokens.size() >= 2 && parserUtils_.iequals(tokens[1], "LIBRARY")) {
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

    if (parserUtils_.iequals(kw, "MACRO")) {
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

    if (parserUtils_.iequals(kw, "CLASS")) {
      // CLASS CORE/BLOCK
      if (tokens.size() >= 2) {
        const std::string& cls = tokens[1];
        if (parserUtils_.iequals(cls, "CORE"))  currentMacroKind_ = ComponentKind::LogicCell;
        else if (parserUtils_.iequals(cls, "BLOCK")) currentMacroKind_ = ComponentKind::Macro;
        else currentMacroKind_ = ComponentKind::Unknown;
      }
    } else if (parserUtils_.iequals(kw, "SIZE")) {
      // SIZE X BY Y ;
      if (tokens.size() >= 4 && parserUtils_.iequals(tokens[2], "BY")) {
        currentMacroWidth_  = std::stof(tokens[1]);
        currentMacroHeight_ = std::stof(tokens[3]);
      } else {
        throw std::runtime_error("Malformed SIZE line in macro " + currentMacroName_);
      }
    } else if (parserUtils_.iequals(kw, "PIN")) {
      // PIN <name>
      if (tokens.size() < 2)
        throw std::runtime_error("PIN without name in macro " + currentMacroName_);

      // On first PIN, we create the macro entry in the library (if not already)
      ensureMacroInLibrary();

      currentPinName_ = tokens[1];
      currentPinHasRect_ = false;
      currentPinDx_ = currentPinDy_ = 0.0f;

      state_ = State::InPin;
    } else if (parserUtils_.iequals(kw, "END")) {
      // END <macro_name>
      if (tokens.size() >= 2 && parserUtils_.iequals(tokens[1], currentMacroName_)) {
        // finalize macro in library (if no pins were encountered)
        ensureMacroInLibrary();
        state_ = State::Idle;
      }
    }
    // ignore: FOREIGN, ORIGIN, SYMMETRY, SITE, etc.
  }

  void CellParser::handleInPin(const std::vector<std::string>& tokens) {
    const std::string& kw = tokens[0];

    if (parserUtils_.iequals(kw, "PORT")) {
      // Begin port region
      state_ = State::InPort;
    } else if (parserUtils_.iequals(kw, "END")) {
      // END <pin_name>
      // When we leave a PIN, if we saw at least one RECT center, store it.
      if (tokens.size() >= 2 && parserUtils_.iequals(tokens[1], currentPinName_)) {
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

    if (parserUtils_.iequals(kw, "END")) {
      // END of PORT
      state_ = State::InPin;
    } else if (parserUtils_.iequals(kw, "RECT")) {
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


  // FloorplanParser

  // Pass 1: parse header & discover sections
  void FloorplanParser::parseFileMetadata() {
    // rewind
    ifs_.clear();
    ifs_.seekg(0, std::ios::beg);

    std::string line;
    bool inDesign = false;

    while (true) {
      std::streampos pos = ifs_.tellg();
      if (!std::getline(ifs_, line)) break;

      parserUtils_.dropComments(line);
      parserUtils_.trimInPlace(line);
      if (line.empty()) continue;

      auto toks = parserUtils_.tokenize(line);
      if (toks.empty()) continue;

      if (!inDesign) {
        // Header fields appear before DESIGN or just after
        if (parserUtils_.iequals(toks[0], "VERSION")) {
          if (toks.size() >= 2) metadata_.version = toks[1];
        } else if (parserUtils_.iequals(toks[0], "DIVIDERCHAR")) {
          if (toks.size() >= 2) metadata_.dividerChar = toks[1];
        } else if (parserUtils_.iequals(toks[0], "BUSBITCHARS")) {
          if (toks.size() >= 2) metadata_.busBitChars = toks[1];
        } else if (parserUtils_.iequals(toks[0], "DESIGN")) {
          if (toks.size() >= 2) metadata_.designName = toks[1];
          inDesign = true;
        } else if (parserUtils_.iequals(toks[0], "UNITS")) {
          // Expect: UNITS DISTANCE MICRONS 1000 ;
          // tokens: [0]=UNITS [1]=DISTANCE [2]=MICRONS [3]=1000 ...
          if (toks.size() >= 4 && parserUtils_.iequals(toks[2], "MICRONS")) {
            metadata_.dbu_per_micron = std::stoi(toks[3]);
          }
        } else {
          continue; // only record sections inside DESIGN
        }
      }

      // Section discovery
      if (parserUtils_.iequals(toks[0], "COMPONENTS")) {
        recordSectionHeader(line, pos, toks, sections_.components);
      } else if (parserUtils_.iequals(toks[0], "PINS")) {
        recordSectionHeader(line, pos, toks, sections_.iopads);
      } else if (parserUtils_.iequals(toks[0], "NETS")) {
        recordSectionHeader(line, pos, toks, sections_.nets);
      } else if (parserUtils_.iequals(toks[0], "VIAS")) {
        recordSectionHeader(line, pos, toks, sections_.vias);
      } else if (parserUtils_.iequals(toks[0], "NONDEFAULTRULES")) {
        recordSectionHeader(line, pos, toks, sections_.nonDefaultRules);
      } else if (parserUtils_.iequals(toks[0], "REGIONS")) {
        recordSectionHeader(line, pos, toks, sections_.regions);
      } else if (parserUtils_.iequals(toks[0], "GROUPS")) {
        recordSectionHeader(line, pos, toks, sections_.groups);
      } else if (parserUtils_.iequals(toks[0], "END") && toks.size() >= 2 &&
          parserUtils_.iequals(toks[1], "DESIGN")) {
        inDesign = false;
        break;
      }
    }
  }

  void FloorplanParser::recordSectionHeader(const std::string& line, std::streampos linePos, const std::vector<std::string>& toks, SectionPos& section) {
    section.beginLinePos = linePos;
    section.present = true;

    // typical: KEYWORD <count> ;
    if (toks.size() >= 2) {
      try {
        section.count = std::stol(toks[1]);
      } catch (...) {
        section.count = -1;
      }
    }
  }

  void FloorplanParser::parseComponents() {
    parseSection("component", sections_.components, [this](const std::vector<std::string>& toks) {parseComponentLine(toks);});
  }

  void FloorplanParser::parseIOPads() {
    parseSection("pin", sections_.iopads, [this](const std::vector<std::string>& toks) {parseIOPadLine(toks);});
  }

  void FloorplanParser::parseSection(const std::string& name, const SectionPos& section, std::function<void(const std::vector<std::string>&)> lineParser) {
    if (!section.present)
      throw std::runtime_error("No " + name + " section found in metadata. Have you parsed the metadata already?");

    ifs_.clear();
    ifs_.seekg(section.beginLinePos);

    std::string line;
    // Consume section header line, ie. "COMPONENTS <count> ;"
    if (!std::getline(ifs_, line)) return;

    std::vector<std::string> buffer = {};

    while (std::getline(ifs_, line)) {
      parserUtils_.dropComments(line);
      parserUtils_.trimInPlace(line);
      if (line.empty()) continue;
      auto toks = parserUtils_.tokenize(line);
      if (toks.empty()) continue;

      // detect section end, ie. END COMPONENTS
      if (parserUtils_.iequals(toks[0], "END") &&
          toks.size() >= 2 &&
          parserUtils_.iequals(toks[1], toUpper(name) + "S")) {

        if (!buffer.empty()) {
          throw std::runtime_error("END " + toUpper(name) + "S before current " + name + " terminated with ';'");
        }
        break; // done with section
      }

      for (auto& tok : toks) {
        if (tok == "-" && !buffer.empty()) {
          throw std::runtime_error("Found new " + name + " start before finishing previous component");
        } else if (tok != ";") {
          buffer.push_back(tok);
        } else {
          buffer.push_back(tok);
          lineParser(buffer);
          buffer.clear();
        }
      }
    }
  }

  void FloorplanParser::parseComponentLine(const std::vector<std::string>& toks) {
    // A component line looks like:
    // - component_name macro_name + UNPLACED ;
    // - component_name macro_name + PLACED (x,y) Orientation ;

    if (toks.empty())
        throwMalformedStatement("component", toks, "empty token list");
    if (toks.front() != "-")
        throwMalformedStatement("component", toks, "first token is not '-'");
    if (toks.back() != ";")
        throwMalformedStatement("component", toks, "last token is not ';'");

    std::string component_name = toks[1];
    std::string macro_name = toks[2];
    uint32_t typeIdx = typeLib_.index_of(macro_name);

    bool placed = false;
    float x = 0.0f, y = 0.0f;

    std::size_t step = 1;
    for (std::size_t i = 3; i < toks.size(); i+=step) {
      // reset step
      step=1;

      if (toks[i] != "+") continue;

      // toks[i] is always "+"
      if (i + 1 >= toks.size()) {
        throwMalformedStatement("component", toks, "Unexpected end after '+' in component");
      }
      const std::string& property = toks[i + 1];

      if (property == "UNPLACED") {
        step = 2;
        // expect: + UNPLACED
        placed = false;
        x = 0.0f;
        y = 0.0f;
      } else if (property == "FIXED") {
        step = 7;
        // expect: + FIXED  (   x   y   )  ORIENT
        // tokens: i  i+1  i+2 i+3 i+4 i+5  i+6
        placed = true;
        if (i + 6 < toks.size() && toks[i+2] == "(" && toks[i+5] == ")") {
          x = std::stof(toks[i+3]);
          y = std::stof(toks[i+4]);
          // orientation in toks[i+6]
        } else {
          throwMalformedStatement("component", toks, "Unexpected FIXED syntax in component");
        }
      } else {
        // other + properties: ignore for now
        // + SOURCE, + REGION, etc...
      }

    }
    ComponentState state = placed ? ComponentState::Placed : ComponentState::Unplaced;
    compLib_.emplace(component_name, typeIdx, x, y, state);
  }

  void FloorplanParser::parseIOPadLine(const std::vector<std::string>& toks) {
    // A Pin line looks like:
    // - pin_name + property + property + ... ;

    if (toks.empty())
        throwMalformedStatement("pin", toks, "empty token list");
    if (toks.front() != "-")
        throwMalformedStatement("pin", toks, "first token is not '-'");
    if (toks.back() != ";")
        throwMalformedStatement("pin", toks, "last token is not ';'");

    std::string pin_name = toks[1];

    bool placed = false;
    float x = 0.0f, y = 0.0f;
    Coordinate pin_ll, pin_ur;

    std::size_t step = 1;
    for (std::size_t i = 2; i < toks.size(); i+=step) {
      // reset step
      step=1;

      if (toks[i] != "+") continue;

      // toks[i] is always "+"
      if (i + 1 >= toks.size()) {
        throwMalformedStatement("pin", toks, "Unexpected end after '+' in IOPad");
      }
      const std::string& property = toks[i + 1];

      if (property == "LAYER") {
        step = 11;
        // expect: + LAYER layer_name (  ll_x ll_y  )   (  ur_x ur_y  )
        // tokens: i  i+1   i+2      i+3 i+4  i+5  i+6 i+7  i+8  i+9 i+10
        pin_ll.x = std::stof(toks[i+4]);
        pin_ll.y = std::stof(toks[i+5]);
        pin_ur.x = std::stof(toks[i+8]);
        pin_ur.y = std::stof(toks[i+9]);
      } else if (property == "PLACED") {
        step = 7;
        // expect: + PLACED  (   x   y   )  ORIENT
        // tokens: i  i+1   i+2 i+3 i+4 i+5  i+6
        placed = true;
        if (i + 6 < toks.size() && toks[i+2] == "(" && toks[i+5] == ")") {
          x = std::stof(toks[i+3]);
          y = std::stof(toks[i+4]);
          // orientation in toks[i+6]
        } else {
          throwMalformedStatement("pin", toks, "Unexpected PLACED syntax in pin");
        }
      } else {
        // other + properties: ignore for now
        // + NET, + DIRECTION, + UNPLACED etc...
      }

    }

    float width = pin_ur.x - pin_ll.x;
    float height = pin_ur.y - pin_ll.y;
    std::string pin_type_name = "IOPin" + std::to_string(static_cast<int>(width)) + "x" + std::to_string(static_cast<int>(height));

    uint32_t typeIdx;
    if (! typeLib_.find_index(pin_type_name, typeIdx)) {
      // add IOPad to component type
      typeIdx = typeLib_.emplace(pin_type_name, ComponentKind::IOPad, width, height);
      // add pin to component type
      ComponentType& ct = typeLib_.at_index(typeIdx);
      ct.pinDx.push_back(width * 0.5f);
      ct.pinDy.push_back(height * 0.5f);
      ct.pinIndex.emplace("pin", 0);
    }

    ComponentState state = placed ? ComponentState::Placed : ComponentState::Unplaced;
    compLib_.emplace(pin_name, typeIdx, x, y, state);
  }

}
