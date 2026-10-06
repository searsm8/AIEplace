/**
 * @file DesignReader.cpp
 * @brief Native LEF / DEF / Bookshelf readers — see DesignReader.h.
 *
 * Every reader loads its whole file into memory and scans it in place: tokens are string_views
 * into the image, and only what a ParseRecord keeps is copied out. The records reproduce what
 * the Limbo adapters handed DataBase, quirks included (each is marked where it is kept), so
 * that the #43 equivalence harness (vck5000/test/parser) can demand field-for-field identity.
 */
#include "DesignReader.h"
#include "DataBase.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#ifdef _OPENMP
#include <omp.h>
#endif

AIEPLACE_NAMESPACE_BEGIN

namespace {

using std::string_view;

// A whole file in memory, NUL-terminated so no scan loop needs a bounds check. Not a std::string:
// resize() would zero-fill hundreds of MB only for fread to overwrite it. Meow.
struct FileImage
{
    std::unique_ptr<char[]> data;
    size_t size = 0;

    bool load(const fs::path& path)
    {
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) { Logger::log_error("cannot read " + path.string()); return false; }
        std::fseek(file, 0, SEEK_END);
        long length = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        size = length > 0 ? (size_t)length : 0;
        data.reset(new char[size + 1]);
        size_t got = size > 0 ? std::fread(data.get(), 1, size, file) : 0;
        std::fclose(file);
        data[size] = '\0';
        if (got != size) { Logger::log_error("short read of " + path.string()); return false; }
        return true;
    }
};

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

bool iequals(string_view a, string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i])) return false;
    return true;
}

string upper(string_view word)
{
    string out(word);
    for (char& c : out) c = (char)std::toupper((unsigned char)c);
    return out;
}

// A scan position in a FileImage. Meow.
class Cursor
{
public:
    Cursor(const FileImage& text, const fs::path& path) : m_begin(text.data.get()), m_pos(m_begin), m_path(path) {}
    // A cursor over one chunk of the image, which the caller has NUL-terminated (readNets). Its
    // errors go to *error, not the log: chunks are read in parallel and Logger is not thread-safe. Meow.
    Cursor(const FileImage& text, const fs::path& path, const char* start, string* error)
        : m_begin(text.data.get()), m_pos(start), m_path(path), m_error(error) {}

    const char* position() const { return m_pos; }
    void seek(const char* pos) { m_pos = pos; }

    // LEF/DEF word: whitespace-delimited; a "quoted string" is one word; '#' at the start of a
    // word comments out the rest of the line. Empty at end of file. Meow.
    string_view word()
    {
        for (;;) {
            while (isSpace(*m_pos)) ++m_pos;
            if (*m_pos != '#') break;
            while (*m_pos && *m_pos != '\n') ++m_pos;
        }
        const char* start = m_pos;
        if (*m_pos == '"') {
            ++m_pos;
            while (*m_pos && *m_pos != '"') m_pos += (*m_pos == '\\' && m_pos[1]) ? 2 : 1;
            if (*m_pos) ++m_pos;
        } else {
            while (*m_pos && !isSpace(*m_pos)) ++m_pos;
        }
        return string_view(start, m_pos - start);
    }

    string_view peekWord()
    {
        const char* saved = m_pos;
        string_view next = word();
        m_pos = saved;
        return next;
    }

    // Bookshelf line: the words of the next non-blank line, ':' always a word of its own (the
    // Limbo scanner's STRING excludes it), '#' to end of line a comment. false at end of file. Meow.
    bool line(std::vector<string_view>& words)
    {
        words.clear();
        for (;;) {
            char c = *m_pos;
            if (c == '\0') return !words.empty();
            if (c == '\n') { ++m_pos; if (!words.empty()) return true; continue; }
            if (isSpace(c)) { ++m_pos; continue; }
            if (c == '#') { while (*m_pos && *m_pos != '\n') ++m_pos; continue; }
            if (c == ':') { words.emplace_back(m_pos, 1); ++m_pos; continue; }
            const char* start = m_pos;
            while (*m_pos && !isSpace(*m_pos) && *m_pos != ':' && *m_pos != '#') ++m_pos;
            words.emplace_back(start, m_pos - start);
        }
    }

    // Logs "<file>:<line>: <what>" and returns false, so a reader can `return fail(...)`. Meow.
    bool fail(const string& what) const
    {
        // '\0' too: readNets turns the newline before each chunk into the chunk's terminator. Meow.
        long line_no = 1 + std::count_if(m_begin, m_pos, [](char c) { return c == '\n' || c == '\0'; });
        string message = m_path.string() + ":" + std::to_string(line_no) + ": " + what;
        if (m_error) *m_error = message;
        else Logger::log_error(message);
        return false;
    }

    // Skip to just past the next ';' word. Meow.
    bool skipStatement()
    {
        for (string_view w = word(); !w.empty(); w = word())
            if (w == ";") return true;
        return fail("missing ';'");
    }

    // Skip to just past the word pair `END <name>`. Meow.
    bool skipBlock(string_view name)
    {
        for (string_view w = word(); !w.empty(); w = word())
            if (w == "END" && peekWord() == name) { word(); return true; }
        return fail("missing 'END " + string(name) + "'");
    }

    bool expect(string_view wanted)
    {
        string_view got = word();
        return got == wanted || fail("expected '" + string(wanted) + "', found '" + string(got) + "'");
    }

private:
    const char* m_begin;
    const char* m_pos;
    fs::path m_path;
    string* m_error = nullptr;
};

constexpr double POW10[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                            1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

// The whole word as a decimal [+-]?[0-9]+(.[0-9]*)?, valued exactly as strtod would (atoi for
// a plain integer). The fast path is exact: mantissa <= 2^53 and 10^k <= 10^22 are both exact
// doubles, so the one division rounds correctly -- the same double strtod returns. Meow.
bool parseDecimal(string_view w, double& out, bool& is_integer)
{
    size_t i = 0;
    bool negative = false;
    if (i < w.size() && (w[i] == '+' || w[i] == '-')) negative = (w[i++] == '-');
    size_t int_start = i;
    unsigned long long mantissa = 0;
    int digits = 0, frac_digits = 0;
    for (; i < w.size() && w[i] >= '0' && w[i] <= '9'; i++, digits++) mantissa = mantissa * 10 + (w[i] - '0');
    if (i == int_start) return false;
    is_integer = (i == w.size());
    if (!is_integer) {
        if (w[i++] != '.') return false;
        for (; i < w.size() && w[i] >= '0' && w[i] <= '9'; i++, digits++, frac_digits++)
            mantissa = mantissa * 10 + (w[i] - '0');
        if (i != w.size()) return false;
    }
    if (is_integer) {
        // Limbo's INTEGER goes through atoi, so a "-0" is +0.0 there, not -0.0. Meow.
        if (digits > 10) return false;
        long long value = negative ? -(long long)mantissa : (long long)mantissa;
        if (value < INT32_MIN || value > INT32_MAX) return false;
        out = (double)(int)value;
        return true;
    }
    if (digits <= 19 && mantissa <= (1ULL << 53) && frac_digits <= 22) {
        double value = (double)mantissa / POW10[frac_digits];
        out = negative ? -value : value;
        return true;
    }
    out = std::strtod(string(w).c_str(), nullptr);
    return true;
}

bool parseInt(string_view w, int& out)
{
    double value;
    bool is_integer;
    if (!parseDecimal(w, value, is_integer) || !is_integer) return false;
    out = (int)value;
    return true;
}

bool parseNumber(string_view w, double& out)
{
    bool is_integer;
    return parseDecimal(w, out, is_integer);
}

// LEF numbers: the Si2 lexer converts them with strtod. Meow.
bool parseLefNumber(string_view w, double& out)
{
    string text(w);
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return !text.empty() && *end == '\0';
}

// DEF numbers: the Si2 grammar reads a NUMBER and ROUNDs it to int. Meow.
bool parseDefInt(string_view w, int& out)
{
    if (parseInt(w, out)) return true;
    string text(w);
    char* end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    if (text.empty() || *end != '\0') return false;
    out = value >= 0 ? (int)(value + 0.5) : (int)(value - 0.5);
    return true;
}

// ============================ parallel chunks ============================

// The first line after the line containing `from` that satisfies is_cut; `end` if none. Meow.
template <typename IsCut>
char* nextCut(char* from, char* end, IsCut is_cut)
{
    for (char* p = from; ; ) {
        p = (char*)std::memchr(p, '\n', end - p);
        if (!p || p + 1 >= end) return end;
        if (is_cut(++p)) return p;
    }
}

constexpr long BLOCK_BYTES = 32L << 20;   // text read per block: bounds the records held at once

#ifdef _OPENMP
inline int chunksPerBlock() { return 4 * omp_get_max_threads(); }
#else
inline int chunksPerBlock() { return 1; }
#endif

// Cut [begin, end) into about chunksPerBlock() chunks at lines satisfying is_cut, and terminate
// each chunk by turning the newline before the next one into '\0'. Returns the chunk starts. Meow.
template <typename IsCut>
std::vector<char*> cutChunks(char* begin, char* end, IsCut is_cut)
{
    std::vector<char*> starts = {begin};
    int count = chunksPerBlock();
    for (int k = 1; k < count; k++) {
        char* start = nextCut(begin + (end - begin) * k / count, end, is_cut);
        if (start > starts.back() && start < end) starts.push_back(start);
    }
    for (size_t i = 1; i < starts.size(); i++) starts[i][-1] = '\0';
    return starts;
}

// The end of the block that starts at `block`: about BLOCK_BYTES on, at a line satisfying is_cut,
// and NUL-terminated there unless it is the end of the file. Meow.
template <typename IsCut>
char* blockEnd(char* block, char* file_end, IsCut is_cut)
{
    if (file_end - block <= BLOCK_BYTES) return file_end;
    char* end = nextCut(block + BLOCK_BYTES, file_end, is_cut);
    if (end < file_end) end[-1] = '\0';
    return end;
}

// Read [begin, end) in blocks, each cut into chunks at lines satisfying is_cut; the chunks of a
// block are tokenised in parallel by read_chunk(chunk_start, chunk), then deliver(chunks) gets
// them in file order -- so the records reach DataBase exactly as a sequential read would send
// them. A Chunk carries its own `error`, logged here rather than from a worker thread. Meow.
template <typename Chunk, typename IsCut, typename ReadChunk, typename Deliver>
bool readInBlocks(char* begin, char* end, IsCut is_cut, ReadChunk read_chunk, Deliver deliver)
{
    std::vector<Chunk> chunks;
    for (char* block = begin; block < end; ) {
        char* block_end = blockEnd(block, end, is_cut);
        std::vector<char*> starts = cutChunks(block, block_end, is_cut);
        chunks.assign(starts.size(), Chunk());
        std::vector<char> ok(starts.size());
        #pragma omp parallel for schedule(dynamic, 1)
        for (long i = 0; i < (long)starts.size(); i++) ok[i] = read_chunk(starts[i], chunks[i]);
        for (size_t i = 0; i < chunks.size(); i++)
            if (!ok[i]) { Logger::log_error(chunks[i].error); return false; }
        deliver(chunks);
        block = block_end;
    }
    return true;
}

bool anyLine(const char*) { return true; }

// ================================== LEF ==================================

bool readLefSite(Cursor& cur, string_view name, DataBase& db)
{
    LefSite site;
    for (string_view w = cur.word(); ; w = cur.word()) {
        if (w.empty()) return cur.fail("SITE " + string(name) + " has no END");
        if (w == "END") {
            if (cur.word() != name) return cur.fail("SITE " + string(name) + " closed by the wrong END");
            db.lef_site_cbk(site);
            return true;
        }
        if (w == "SIZE") {
            if (!parseLefNumber(cur.word(), site.size_x) || cur.word() != "BY" ||
                !parseLefNumber(cur.word(), site.size_y)) return cur.fail("malformed SITE SIZE");
            site.has_size = true;
        } else if (w == "CLASS") {
            site.has_class = true;
            site.site_class = upper(cur.word());
        }
        if (!cur.skipStatement()) return false;
    }
}

bool readLefPort(Cursor& cur, LefPin& pin, bool record_rect)
{
    for (string_view w = cur.word(); ; w = cur.word()) {
        if (w.empty()) return cur.fail("PORT has no END");
        if (w == "END") return true;
        if (w == "RECT" && record_rect && !pin.has_rect) {
            string_view first = cur.word();
            if (first == "ITERATE") { if (!cur.skipStatement()) return false; continue; }
            if (first == "MASK") { cur.word(); first = cur.word(); }
            if (!parseLefNumber(first, pin.rect_xl) || !parseLefNumber(cur.word(), pin.rect_yl) ||
                !parseLefNumber(cur.word(), pin.rect_xh) || !parseLefNumber(cur.word(), pin.rect_yh))
                return cur.fail("malformed PORT RECT");
            pin.has_rect = true;
        }
        if (!cur.skipStatement()) return false;
    }
}

bool readLefPin(Cursor& cur, string_view name, DataBase& db)
{
    LefPin pin;
    pin.name = string(name);
    bool seen_port = false;
    for (string_view w = cur.word(); ; w = cur.word()) {
        if (w.empty()) return cur.fail("PIN " + pin.name + " has no END");
        if (w == "END") {
            if (cur.word() != name) return cur.fail("PIN " + pin.name + " closed by the wrong END");
            db.lef_pin_cbk(pin);
            return true;
        }
        if (w == "PORT") {
            // Only the first PORT's first RECT is kept -- what DataBase took from Limbo's port(0). Meow.
            if (!readLefPort(cur, pin, !seen_port)) return false;
            seen_port = true;
            continue;
        }
        if (w == "USE") pin.use = upper(cur.word());
        if (!cur.skipStatement()) return false;
    }
}

// OBS / DENSITY: geometry blocks closed by a bare END. Meow.
bool skipBareEndBlock(Cursor& cur, string_view what)
{
    for (string_view w = cur.word(); !w.empty(); w = cur.word())
        if (w == "END") return true;
    return cur.fail(string(what) + " has no END");
}

bool readLefMacro(Cursor& cur, string_view name, DataBase& db)
{
    db.lef_macrobegin_cbk(string(name));
    LefMacro macro;
    for (string_view w = cur.word(); ; w = cur.word()) {
        if (w.empty()) return cur.fail("MACRO " + string(name) + " has no END");
        if (w == "END") {
            if (cur.word() != name) return cur.fail("MACRO " + string(name) + " closed by the wrong END");
            db.lef_macro_cbk(macro);
            return true;
        }
        if (w == "PIN") {
            if (!readLefPin(cur, cur.word(), db)) return false;
        } else if (w == "OBS" || w == "DENSITY") {
            if (!skipBareEndBlock(cur, w)) return false;
        } else if (w == "SIZE") {
            if (!parseLefNumber(cur.word(), macro.size_x) || cur.word() != "BY" ||
                !parseLefNumber(cur.word(), macro.size_y)) return cur.fail("malformed MACRO SIZE");
            if (!cur.skipStatement()) return false;
        } else if (w == "CLASS") {
            macro.has_class = true;
            macro.macro_class.clear();
            for (string_view word = cur.word(); word != ";"; word = cur.word()) {
                if (word.empty()) return cur.fail("missing ';'");
                if (!macro.macro_class.empty()) macro.macro_class += ' ';
                macro.macro_class += upper(word);
            }
        } else if (!cur.skipStatement()) {
            return false;
        }
    }
}

// ================================== DEF ==================================

// `( x y )`. Meow.
bool readDefPoint(Cursor& cur, int& x, int& y)
{
    return cur.word() == "(" && parseDefInt(cur.word(), x) && parseDefInt(cur.word(), y) && cur.word() == ")";
}

bool readDefCount(Cursor& cur, int& count)
{
    return (parseDefInt(cur.word(), count) && cur.word() == ";") || cur.fail("malformed section count");
}

// Skip one `+ OPTION ...` up to (not past) the next '+' or ';'. Meow.
bool skipDefOption(Cursor& cur)
{
    for (string_view w = cur.peekWord(); ; w = cur.peekWord()) {
        if (w.empty()) return cur.fail("missing ';'");
        if (w == "+" || w == ";") return true;
        cur.word();
    }
}

// A line whose first word is "-": where a DEF COMPONENTS / NETS entry begins. Meow.
bool isDefEntryLine(const char* line)
{
    while (*line == ' ' || *line == '\t' || *line == '\r') ++line;
    return line[0] == '-' && isSpace(line[1]);
}

// The `END <section>` line closing a DEF section that starts at `from`; null if none. Meow.
char* defSectionEnd(char* from, char* file_end, string_view section)
{
    auto is_end = [section](const char* line) {
        while (*line == ' ' || *line == '\t' || *line == '\r') ++line;
        if (std::strncmp(line, "END", 3) != 0 || !isSpace(line[3])) return false;
        line += 3;
        while (*line == ' ' || *line == '\t') ++line;
        return std::strncmp(line, section.data(), section.size()) == 0 &&
               (isSpace(line[section.size()]) || line[section.size()] == '\0');
    };
    char* end = nextCut(from, file_end, is_end);
    return end < file_end ? end : nullptr;
}

// Read the entries of a DEF section in blocks of parallel chunks cut at entry lines (as
// readNets does), handing each block's records to `deliver` in file order; then resume `cur`
// at the section's END line. read_chunk(start, chunk) tokenises one NUL-terminated chunk. Meow.
template <typename Chunk, typename ReadChunk, typename Deliver>
bool readDefSection(Cursor& cur, FileImage& text, string_view section, ReadChunk read_chunk, Deliver deliver)
{
    char* const file_end = text.data.get() + text.size;
    char* begin = text.data.get() + (cur.position() - text.data.get());
    char* section_end = defSectionEnd(begin, file_end, section);
    if (!section_end) return cur.fail("missing 'END " + string(section) + "'");
    section_end[-1] = '\0';
    if (!readInBlocks<Chunk>(begin, section_end, isDefEntryLine, read_chunk, deliver)) return false;
    cur.seek(section_end);
    return cur.expect("END") && cur.expect(section);
}

struct DefComponentChunk {
    std::vector<DefComponent> components;
    std::vector<char> located;   // whether the entry gave a location (PLACED/FIXED/COVER/UNPLACED)
    string error;
};

bool readDefComponents(Cursor& cur, FileImage& text, const fs::path& path, DataBase& db)
{
    int count;
    if (!readDefCount(cur, count)) return false;
    auto read_chunk = [&](const char* start, DefComponentChunk& chunk) {
        Cursor in(text, path, start, &chunk.error);
        for (string_view w = in.word(); !w.empty(); w = in.word()) {
            if (w != "-") return in.fail("expected '-' or 'END COMPONENTS', found '" + string(w) + "'");
            DefComponent& comp = chunk.components.emplace_back();
            bool located = false;
            comp.name = in.word();
            comp.macro_name = in.word();
            for (string_view opt = in.word(); opt != ";"; opt = in.word()) {
                if (opt != "+") return in.fail("expected '+' or ';' in COMPONENT " + string(comp.name));
                string_view kw = in.word();
                if (kw == "PLACED" || kw == "FIXED" || kw == "COVER") {
                    if (!readDefPoint(in, comp.x, comp.y)) return in.fail("malformed placement of " + string(comp.name));
                    in.word();  // orient: DataBase does not keep a DEF component's orientation
                    comp.status = kw;
                    located = true;
                } else if (kw == "UNPLACED") {
                    // DEF >= 5.4 ignores a point after UNPLACED; skipDefOption drops it. Meow.
                    comp.status = "UNPLACED";
                    comp.x = comp.y = -1;
                    located = true;
                    if (!skipDefOption(in)) return false;
                } else if (!skipDefOption(in)) {
                    return false;
                }
            }
            chunk.located.push_back(located);
        }
        return true;
    };
    // Si2's defiComponent::clear() leaves x_/y_ alone, so a component with no placement option
    // inherits the previous one's location -- across chunk boundaries too, hence done here, in
    // order. Meow.
    int x = -1, y = -1;
    std::vector<DefComponent> components;
    auto deliver = [&](std::vector<DefComponentChunk>& chunks) {
        components.clear();
        for (DefComponentChunk& chunk : chunks)
            for (size_t i = 0; i < chunk.components.size(); i++) {
                DefComponent& comp = chunk.components[i];
                if (chunk.located[i]) { x = comp.x; y = comp.y; } else { comp.x = x; comp.y = y; }
                components.push_back(comp);
            }
        db.add_def_components(components);
    };
    return readDefSection<DefComponentChunk>(cur, text, "COMPONENTS", read_chunk, deliver);
}

bool readDefPins(Cursor& cur, DataBase& db)
{
    int count;
    if (!readDefCount(cur, count)) return false;
    DefPin pin;
    for (string_view w = cur.word(); ; w = cur.word()) {
        if (w == "END") return cur.expect("PINS");
        if (w != "-") return cur.fail("expected '-' or 'END PINS', found '" + string(w) + "'");
        pin = DefPin();
        pin.name = string(cur.word());
        for (string_view opt = cur.word(); opt != ";"; opt = cur.word()) {
            if (opt != "+") return cur.fail("expected '+' or ';' in PIN " + pin.name);
            string_view kw = cur.word();
            if (kw == "DIRECTION") {
                pin.direction = string(cur.word());
            } else if (kw == "LAYER") {
                cur.word();  // layer name
                while (cur.peekWord() == "MASK" || cur.peekWord() == "SPACING" || cur.peekWord() == "DESIGNRULEWIDTH") {
                    cur.word();
                    cur.word();
                }
                int bbox[4];
                if (!readDefPoint(cur, bbox[0], bbox[1]) || !readDefPoint(cur, bbox[2], bbox[3]))
                    return cur.fail("malformed LAYER of PIN " + pin.name);
                if (!pin.has_layer) std::copy(bbox, bbox + 4, pin.bbox);
                pin.has_layer = true;
            } else if (kw == "PLACED" || kw == "FIXED" || kw == "COVER") {
                if (!readDefPoint(cur, pin.x, pin.y)) return cur.fail("malformed placement of PIN " + pin.name);
                cur.word();  // orient
                // Limbo maps PLACED and FIXED only; a COVER pin keeps its location but no status. Meow.
                pin.status = (kw == "COVER") ? "" : string(kw);
            } else if (kw == "UNPLACED") {
                pin.status = "UNPLACED";
                pin.x = pin.y = 0;  // defiPin::clear() zeroes the location
                if (!skipDefOption(cur)) return false;
            } else if (kw == "PORT") {
                return cur.fail("PIN " + pin.name + ": '+ PORT' pins are not supported");
            } else if (!skipDefOption(cur)) {
                return false;
            }
        }
        // Limbo read vBbox.front() unchecked; a pin with no LAYER was undefined behaviour there. Meow.
        if (!pin.has_layer) return cur.fail("PIN " + pin.name + " has no LAYER");
        db.add_def_pin(pin);
    }
}

struct DefNetChunk {
    std::vector<DefNetPin> pins;
    std::vector<std::pair<string_view, size_t>> nets;   // (name, first pin)
    string error;
};

bool readDefNets(Cursor& cur, FileImage& text, const fs::path& path, DataBase& db)
{
    int count;
    if (!readDefCount(cur, count)) return false;
    auto read_chunk = [&](const char* start, DefNetChunk& chunk) {
        Cursor in(text, path, start, &chunk.error);
        for (string_view w = in.word(); !w.empty(); w = in.word()) {
            if (w != "-") return in.fail("expected '-' or 'END NETS', found '" + string(w) + "'");
            string_view name = in.word();
            chunk.nets.emplace_back(name, chunk.pins.size());
            for (string_view item = in.word(); item != ";"; item = in.word()) {
                if (item == "(") {
                    string_view instance = in.word(), pin_name = in.word();
                    for (string_view close = in.word(); close != ")"; close = in.word())   // e.g. + SYNTHESIZED
                        if (close.empty()) return in.fail("unclosed connection in NET " + string(name));
                    chunk.pins.emplace_back(instance, pin_name);
                } else if (item == "+") {
                    // Routing and properties: nothing DataBase keeps, and they end the connections. Meow.
                    if (!in.skipStatement()) return false;
                    break;
                } else {
                    return in.fail("unexpected '" + string(item) + "' in NET " + string(name));
                }
            }
        }
        return true;
    };
    std::vector<DefNet> nets;
    auto deliver = [&](std::vector<DefNetChunk>& chunks) {
        nets.clear();
        for (const DefNetChunk& chunk : chunks)
            for (size_t j = 0; j < chunk.nets.size(); j++) {
                size_t first = chunk.nets[j].second;
                size_t last = j + 1 < chunk.nets.size() ? chunk.nets[j + 1].second : chunk.pins.size();
                nets.push_back({chunk.nets[j].first, chunk.pins.data() + first, last - first});
            }
        db.add_def_nets(nets);
    };
    return readDefSection<DefNetChunk>(cur, text, "NETS", read_chunk, deliver);
}

// Sections whose contents DataBase does not use: skipped to their `END <name>`. Meow.
bool isSkippedDefSection(string_view w)
{
    static const string_view SECTIONS[] = {"VIAS", "STYLES", "NONDEFAULTRULES", "PINPROPERTIES", "BLOCKAGES",
        "SLOTS", "FILLS", "SPECIALNETS", "SCANCHAINS", "IOTIMINGS", "FLOORPLANCONSTRAINTS",
        "TIMINGDISABLES", "PARTITIONS", "PROPERTYDEFINITIONS"};
    return std::find(std::begin(SECTIONS), std::end(SECTIONS), w) != std::end(SECTIONS);
}

// ================================ Bookshelf ================================

using Words = std::vector<string_view>;

// "UCLA <kind> <version>" or "<kind> <version>"; true when `words` is that header line. Meow.
bool isBookshelfHeader(const Words& words, string_view kind)
{
    size_t at = (!words.empty() && iequals(words[0], "UCLA")) ? 1 : 0;
    return words.size() == at + 2 && iequals(words[at], kind);
}

bool readBookshelfHeader(Cursor& cur, Words& words, string_view kind)
{
    return (cur.line(words) && isBookshelfHeader(words, kind)) || cur.fail("missing '" + string(kind) + "' header");
}

// `Key : <int>` -- e.g. NumNodes : 211447. Meow.
bool isCountLine(const Words& words, std::initializer_list<string_view> keys)
{
    if (words.size() != 3 || words[1] != ":") return false;
    for (string_view key : keys) if (iequals(words[0], key)) return true;
    return false;
}

// The body of a Bookshelf file after its header line, as the [begin, end) readInBlocks takes. Meow.
std::pair<char*, char*> bookshelfBody(FileImage& text, const Cursor& after_header)
{
    char* begin = text.data.get();
    return {begin + (after_header.position() - begin), begin + text.size};
}

struct NodeChunk {
    std::vector<BookshelfNode> nodes;
    string error;
};

bool readNodeChunk(const FileImage& text, const fs::path& path, const char* start, NodeChunk& chunk)
{
    Cursor cur(text, path, start, &chunk.error);
    Words words;
    while (cur.line(words)) {
        if (isCountLine(words, {"NumNodes", "NumTerminals"})) continue;
        BookshelfNode& node = chunk.nodes.emplace_back();
        if ((words.size() != 3 && words.size() != 4) || !parseInt(words[1], node.width) || !parseInt(words[2], node.height))
            return cur.fail("expected '<name> <width> <height> [terminal]'");
        node.name = words[0];
        if (words.size() == 4 && iequals(words[3], "terminal_NI")) return cur.fail("terminal_NI is not supported");
        node.terminal = words.size() == 4 && iequals(words[3], "terminal");
    }
    return true;
}

// Every .nodes line stands alone, so any line is a cut. Meow.
bool readNodes(const fs::path& path, DataBase& db)
{
    FileImage text;
    if (!text.load(path)) return false;
    Cursor cur(text, path);
    Words words;
    if (!readBookshelfHeader(cur, words, "nodes")) return false;
    auto [begin, end] = bookshelfBody(text, cur);
    std::vector<BookshelfNode> nodes;
    return readInBlocks<NodeChunk>(begin, end, anyLine,
        [&](const char* start, NodeChunk& chunk) { return readNodeChunk(text, path, start, chunk); },
        [&](std::vector<NodeChunk>& chunks) {
            nodes.clear();
            for (const NodeChunk& chunk : chunks) nodes.insert(nodes.end(), chunk.nodes.begin(), chunk.nodes.end());
            db.add_bookshelf_nodes(nodes);
        });
}

// One chunk of a .nets file, tokenised: its pins, and its nets as (name, first pin). Meow.
struct NetChunk {
    std::vector<BookshelfNetPin> pins;
    std::vector<std::pair<string_view, size_t>> nets;
    string error;
};

bool readNetChunk(const FileImage& text, const fs::path& path, const char* start, NetChunk& chunk)
{
    Cursor cur(text, path, start, &chunk.error);
    Words words;
    while (cur.line(words)) {
        if (isCountLine(words, {"NumNets", "NumPins"})) continue;
        if (iequals(words[0], "NetDegree")) {
            int degree;
            if (words.size() != 4 || words[1] != ":" || !parseInt(words[2], degree))
                return cur.fail("expected 'NetDegree : <n> <name>'");
            chunk.nets.emplace_back(words[3], chunk.pins.size());
            continue;
        }
        if (chunk.nets.empty()) return cur.fail("pin line before the first NetDegree");
        // <node> <I|O|B> : <x> <y> [: <w> <h> [<pin>]]
        bool valid_direction = words.size() >= 2 && words[1].size() == 1 &&
                               std::strchr("IOBiob", words[1][0]) != nullptr;
        if (!valid_direction || (words.size() != 5 && words.size() != 8 && words.size() != 9) || words[2] != ":")
            return cur.fail("expected '<node> <I|O|B> : <x> <y>'");
        BookshelfNetPin& pin = chunk.pins.emplace_back();
        pin.node_name = words[0];
        double width, height;
        if (!parseNumber(words[3], pin.offset_x) || !parseNumber(words[4], pin.offset_y) ||
            (words.size() >= 8 && (words[5] != ":" || !parseNumber(words[6], width) || !parseNumber(words[7], height))))
            return cur.fail("malformed pin offset");
        if (words.size() == 9) pin.pin_name = words[8];
    }
    return true;
}

// Does the line beginning at `line` start with the word NetDegree, as Cursor::line would split it? Meow.
bool isNetDegreeLine(const char* line)
{
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\f' || *line == '\v') ++line;
    for (char c : string_view("netdegree"))
        if (std::tolower((unsigned char)*line++) != c) return false;
    return isSpace(*line) || *line == ':' || *line == '#' || *line == '\0';
}

// A NetDegree line is a safe cut because a net ends only where the next begins: Limbo emits a
// net with however many pin lines followed it, and the declared degree only reserves. Meow.
bool readNets(const fs::path& path, DataBase& db)
{
    FileImage text;
    if (!text.load(path)) return false;
    Cursor cur(text, path);
    Words words;
    if (!readBookshelfHeader(cur, words, "nets")) return false;
    auto [begin, end] = bookshelfBody(text, cur);
    std::vector<BookshelfNet> nets;
    return readInBlocks<NetChunk>(begin, end, isNetDegreeLine,
        [&](const char* start, NetChunk& chunk) { return readNetChunk(text, path, start, chunk); },
        [&](std::vector<NetChunk>& chunks) {
            nets.clear();
            for (const NetChunk& chunk : chunks)
                for (size_t j = 0; j < chunk.nets.size(); j++) {
                    size_t first = chunk.nets[j].second;
                    size_t last = j + 1 < chunk.nets.size() ? chunk.nets[j + 1].second : chunk.pins.size();
                    nets.push_back({chunk.nets[j].first, chunk.pins.data() + first, last - first});
                }
            db.add_bookshelf_nets(nets);
        });
}

// The entry of `canon` equal to `word` ignoring case (the Limbo scanner's keywords are case-
// insensitive), or an empty view. Meow.
template <size_t N>
string_view canonical(string_view word, const string_view (&canon)[N])
{
    for (string_view entry : canon) if (iequals(word, entry)) return entry;
    return {};
}

struct PlChunk {
    std::vector<BookshelfPlacement> placements;
    string error;
};

bool readPlChunk(const FileImage& text, const fs::path& path, const char* start, PlChunk& chunk)
{
    static const string_view ORIENTS[] = {"N", "S", "W", "E", "FN", "FS", "FW", "FE"};
    static const string_view STATUSES[] = {"FIXED", "FIXED_NI", "PLACED", "UNPLACED"};
    Cursor cur(text, path, start, &chunk.error);
    Words words;
    while (cur.line(words)) {
        // <name> <x> <y> : <orient> [/FIXED]
        BookshelfPlacement& placement = chunk.placements.emplace_back();
        if ((words.size() != 5 && words.size() != 6) || !parseNumber(words[1], placement.x) ||
            !parseNumber(words[2], placement.y) || words[3] != ":")
            return cur.fail("expected '<name> <x> <y> : <orient> [/FIXED]'");
        placement.name = words[0];
        placement.orient = canonical(words[4], ORIENTS);
        if (placement.orient.empty()) return cur.fail("unknown orientation '" + string(words[4]) + "'");
        if (words.size() == 6) {
            string_view word = words[5];
            if (!word.empty() && word[0] == '/') word.remove_prefix(1);
            placement.status = canonical(word, STATUSES);
            if (placement.status.empty()) return cur.fail("unknown placement status '" + string(words[5]) + "'");
        }
    }
    return true;
}

// Every .pl line stands alone, so any line is a cut. Meow.
bool readPl(const fs::path& path, DataBase& db)
{
    FileImage text;
    if (!text.load(path)) return false;
    Cursor cur(text, path);
    Words words;
    if (!readBookshelfHeader(cur, words, "pl")) return false;
    auto [begin, end] = bookshelfBody(text, cur);
    std::vector<BookshelfPlacement> placements;
    return readInBlocks<PlChunk>(begin, end, anyLine,
        [&](const char* start, PlChunk& chunk) { return readPlChunk(text, path, start, chunk); },
        [&](std::vector<PlChunk>& chunks) {
            placements.clear();
            for (const PlChunk& chunk : chunks)
                placements.insert(placements.end(), chunk.placements.begin(), chunk.placements.end());
            db.set_bookshelf_node_positions(placements);
        });
}

bool readScl(const fs::path& path, DataBase& db)
{
    FileImage text;
    if (!text.load(path)) return false;
    Cursor cur(text, path);
    Words words;
    if (!readBookshelfHeader(cur, words, "scl")) return false;
    BookshelfRow row;
    bool in_row = false;
    while (cur.line(words)) {
        if (!in_row) {
            if (isCountLine(words, {"NumRows"})) continue;
            if (words.size() != 2 || !iequals(words[0], "CoreRow") ||
                !(iequals(words[1], "Horizontal") || iequals(words[1], "Vertical")))
                return cur.fail("expected 'CoreRow Horizontal'");
            row = BookshelfRow();
            in_row = true;
            continue;
        }
        if (words.size() == 1 && iequals(words[0], "End")) {
            db.add_bookshelf_row(row);
            in_row = false;
            continue;
        }
        // One or more `Key : value` pairs per line.
        if (words.size() % 3 != 0) return cur.fail("expected 'Key : value' pairs");
        for (size_t i = 0; i < words.size(); i += 3) {
            string_view key = words[i];
            if (words[i + 1] != ":") return cur.fail("expected ':' after '" + string(key) + "'");
            int* field = iequals(key, "Coordinate")   ? &row.origin_y
                       : iequals(key, "SubrowOrigin") ? &row.origin_x
                       : iequals(key, "Height")       ? &row.height
                       : iequals(key, "Sitewidth")    ? &row.site_width
                       : iequals(key, "Sitespacing")  ? &row.site_spacing
                       : iequals(key, "NumSites")     ? &row.site_num
                       : nullptr;
            if (field) {
                if (!parseInt(words[i + 2], *field)) return cur.fail("'" + string(key) + "' needs an integer");
            } else if (!iequals(key, "Siteorient") && !iequals(key, "Sitesymmetry")) {
                return cur.fail("unknown CoreRow property '" + string(key) + "'");
            }
        }
    }
    return !in_row || cur.fail("CoreRow has no End");
}

// A .wts with any entry is rejected: Limbo's base callback for net weights calls exit(0). Meow.
bool readWts(const fs::path& path, DataBase&)
{
    FileImage text;
    if (!text.load(path)) return false;
    Cursor cur(text, path);
    Words words;
    if (!readBookshelfHeader(cur, words, "wts")) return false;
    return !cur.line(words) || cur.fail("net weights are not supported");
}

} // namespace

bool readLefFile(const fs::path& lef_file, DataBase& db)
{
    FileImage text;
    if (!text.load(lef_file)) return false;
    Cursor cur(text, lef_file);
    for (string_view w = cur.word(); !w.empty(); w = cur.word()) {
        if (w == "END") {
            string_view what = cur.word();
            if (what == "LIBRARY") return true;
            return cur.fail("unexpected 'END " + string(what) + "'");
        }
        if (w == "MACRO") {
            if (!readLefMacro(cur, cur.word(), db)) return false;
        } else if (w == "SITE") {
            if (!readLefSite(cur, cur.word(), db)) return false;
        } else if (w == "LAYER" || w == "VIA" || w == "VIARULE" || w == "NONDEFAULTRULE" || w == "ARRAY") {
            if (!cur.skipBlock(cur.word())) return false;
        } else if (w == "UNITS" || w == "PROPERTYDEFINITIONS" || w == "SPACING" || w == "NOISETABLE" ||
                   w == "CORRECTIONTABLE" || w == "IRDROP") {
            if (!cur.skipBlock(w)) return false;
        } else if (w == "BEGINEXT") {
            for (w = cur.word(); !w.empty() && w != "ENDEXT"; w = cur.word()) {}
            if (w.empty()) return cur.fail("BEGINEXT has no ENDEXT");
        } else if (!cur.skipStatement()) {
            return false;
        }
    }
    return true;  // END LIBRARY is optional from LEF 5.6
}

bool readDefFile(const fs::path& def_file, DataBase& db)
{
    FileImage text;
    if (!text.load(def_file)) return false;
    Cursor cur(text, def_file);
    for (string_view w = cur.word(); ; w = cur.word()) {
        if (w.empty()) return cur.fail("missing 'END DESIGN'");
        if (w == "END") {
            string_view what = cur.word();
            if (what == "DESIGN") return true;
            return cur.fail("unexpected 'END " + string(what) + "'");
        }
        if (w == "DESIGN") {
            db.set_def_design(string(cur.word()));
            if (!cur.skipStatement()) return false;
        } else if (w == "UNITS") {
            double units;
            if (cur.word() != "DISTANCE" || cur.word() != "MICRONS" || !parseLefNumber(cur.word(), units))
                return cur.fail("expected 'UNITS DISTANCE MICRONS <n>'");
            db.set_def_unit((int)units);
            if (!cur.skipStatement()) return false;
        } else if (w == "DIEAREA") {
            // Si2's defiBox keeps the first two points as (xl,yl) and (xh,yh), as written. Meow.
            int xl = 0, yl = 0, xh = 0, yh = 0, x, y, points = 0;
            while (cur.peekWord() == "(") {
                if (!readDefPoint(cur, x, y)) return cur.fail("malformed DIEAREA point");
                if (points == 0) { xl = x; yl = y; } else if (points == 1) { xh = x; yh = y; }
                points++;
            }
            if (points < 2 || !cur.expect(";")) return cur.fail("malformed DIEAREA");
            db.set_def_diearea(xl, yl, xh, yh);
        } else if (w == "COMPONENTS") {
            if (!readDefComponents(cur, text, def_file, db)) return false;
        } else if (w == "PINS") {
            if (!readDefPins(cur, db)) return false;
        } else if (w == "NETS") {
            if (!readDefNets(cur, text, def_file, db)) return false;
        } else if (w == "REGIONS" || w == "GROUPS") {
            int count;
            if (!readDefCount(cur, count)) return false;
            if (w == "REGIONS") db.resize_def_region(count); else db.resize_def_group(count);
            if (!cur.skipBlock(w)) return false;
        } else if (isSkippedDefSection(w)) {
            if (!cur.skipBlock(w)) return false;
        } else if (w == "BEGINEXT") {
            for (w = cur.word(); !w.empty() && w != "ENDEXT"; w = cur.word()) {}
            if (w.empty()) return cur.fail("BEGINEXT has no ENDEXT");
        } else if (!cur.skipStatement()) {   // VERSION, DIVIDERCHAR, BUSBITCHARS, ROW, TRACKS, GCELLGRID, ...
            return false;
        }
    }
}

bool readBookshelfAux(const fs::path& aux_file, DataBase& db)
{
    FileImage text;
    if (!text.load(aux_file)) return false;
    Cursor cur(text, aux_file);
    Words words;
    // <design> : <file> <file> ...
    if (!cur.line(words) || words.size() < 3 || words[1] != ":")
        return cur.fail("expected '<design> : <files>'");
    db.set_bookshelf_design(string(words[0]));

    // Limbo's visit order, by suffix; a suffix outside it is an error rather than a guess. Meow.
    static const string_view ORDER[] = {".scl", ".nodes", ".nets", ".wts", ".pl"};
    using Reader = bool (*)(const fs::path&, DataBase&);
    static const Reader READERS[] = {readScl, readNodes, readNets, readWts, readPl};
    std::vector<std::pair<int, fs::path>> files;
    for (size_t i = 2; i < words.size(); i++) {
        fs::path file = aux_file.parent_path() / string(words[i]);
        string suffix = file.extension().string();
        auto at = std::find_if(std::begin(ORDER), std::end(ORDER), [&](string_view s) { return iequals(s, suffix); });
        if (at == std::end(ORDER)) return cur.fail("unsupported bookshelf file '" + string(words[i]) + "'");
        files.emplace_back((int)(at - std::begin(ORDER)), file);
    }
    std::stable_sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [kind, file] : files)
        if (!READERS[kind](file, db)) return false;
    db.bookshelf_end();
    return true;
}

AIEPLACE_NAMESPACE_END
