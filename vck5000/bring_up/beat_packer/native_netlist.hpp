#ifndef NATIVE_NETLIST_HPP
#define NATIVE_NETLIST_HPP

// native_netlist.hpp -- packer::Netlist read by the host's own design reader (#43,
// host/src/common/src/DesignReader.cpp) instead of the stream readers in beat_packer.hpp.
//
// It builds EXACTLY the Netlist read_bookshelf / read_def build -- the same node order (file
// order; a DEF IO pin when a net first names it), the same macro rule, the same offsets bit for
// bit, the same kept nets -- and beat_packer --check-reader holds it to that on every design.
// What it adds is speed (the host reader runs its chunks in parallel) and a second, independent
// reader under the encoder.
//
// It needs the host's common/ sources linked (DesignReader, Logger, Grid, Net, Common; see the
// Makefile), so beat_packer.hpp does NOT include it: the tier-1 harnesses and the HLS testbenches
// stay header-only on the legacy readers. Meow.

#include "beat_packer.hpp"
#include "DesignReader.h"

namespace packer {

class NetlistSink : public AIEplace::DesignSink
{
public:
    Netlist nl;
    std::string error;   // first error, if any; the readers' own errors go to the log

    // ---- Bookshelf: "terminal" in .nodes = fixed; a movable node taller than the most common
    // ---- movable height is a macro; pin offsets as written in .nets.
    void add_bookshelf_nodes(const std::vector<AIEplace::BookshelfNode>& nodes) override
    {
        for (const AIEplace::BookshelfNode& node : nodes) {
            node_id.emplace(std::string(node.name), (int)nl.movable.size());
            nl.movable.push_back(node.terminal ? 0 : 1);
            height.push_back(node.height);
        }
    }

    void add_bookshelf_nets(const std::vector<AIEplace::BookshelfNet>& nets) override
    {
        std::vector<size_t> first_pin(nets.size() + 1, 0);
        for (size_t i = 0; i < nets.size(); i++) first_pin[i + 1] = first_pin[i] + nets[i].num_pins;
        std::vector<int> pin_node(first_pin.back());
        // Name lookups only read node_id, so they run in parallel; offsets are keyed in order
        // below, since a key is the order an offset value is first seen. Meow.
        #pragma omp parallel for schedule(dynamic, 256) if(nets.size() >= 4096)
        for (long i = 0; i < (long)nets.size(); i++)
            for (size_t k = 0; k < nets[i].num_pins; k++) {
                auto it = node_id.find(std::string(nets[i].pins[k].node_name));
                pin_node[first_pin[i] + k] = it == node_id.end() ? -1 : it->second;
            }
        std::vector<Pin> net;
        for (size_t i = 0; i < nets.size(); i++) {
            net.clear();
            for (size_t k = 0; k < nets[i].num_pins; k++) {
                const AIEplace::BookshelfNetPin& p = nets[i].pins[k];
                Pin pin;
                pin.node = pin_node[first_pin[i] + k];
                if (pin.node < 0) return fail("unknown node " + std::string(p.node_name));
                pin.offset_key[0] = dict[0].key((float)p.offset_x, nl.offset_value[0]);
                pin.offset_key[1] = dict[1].key((float)p.offset_y, nl.offset_value[1]);
                net.push_back(pin);
            }
            keep(net);
        }
    }

    // ---- DEF: COMPONENTS "+ FIXED" / "+ COVER" = fixed; LEF CLASS BLOCK = macro; "( PIN x )"
    // ---- is its own fixed IO node at offset 0; a component pin's offset is the centre of the
    // ---- first RECT of its first PORT in cells.lef, in microns as written.
    void lef_macrobegin_cbk(const std::string& name) override { current_macro = &lef[name]; }
    void lef_macro_cbk(const AIEplace::LefMacro& m) override
    {
        current_macro->block = m.has_class && m.macro_class.substr(0, m.macro_class.find(' ')) == "BLOCK";
    }
    void lef_pin_cbk(const AIEplace::LefPin& p) override
    {
        if (!p.has_rect || current_macro->pin_offset.count(p.name)) return;
        float x0 = (float)p.rect_xl, y0 = (float)p.rect_yl, x1 = (float)p.rect_xh, y1 = (float)p.rect_yh;
        current_macro->pin_offset[p.name] = {(x0 + x1) / 2.0f, (y0 + y1) / 2.0f};
    }

    void add_def_components(const std::vector<AIEplace::DefComponent>& components) override
    {
        for (const AIEplace::DefComponent& c : components) {
            std::string name(c.name), master(c.macro_name);
            node_id.emplace(name, (int)nl.movable.size());
            master_of.emplace(name, master);
            nl.movable.push_back(c.status == "FIXED" || c.status == "COVER" ? 0 : 1);
            auto it = lef.find(master);
            nl.is_macro.push_back(it != lef.end() && it->second.block);
        }
    }

    void add_def_nets(const std::vector<AIEplace::DefNet>& nets) override
    {
        std::vector<size_t> first_pin(nets.size() + 1, 0);
        for (size_t i = 0; i < nets.size(); i++) first_pin[i + 1] = first_pin[i] + nets[i].num_pins;
        // Component pins: node and LEF offset, read-only lookups, in parallel. IO pins become
        // nodes in the order nets first name them, so they wait for the in-order pass. Meow.
        std::vector<int> pin_node(first_pin.back(), -1);
        std::vector<const std::array<float, 2>*> pin_offset(first_pin.back(), nullptr);
        #pragma omp parallel for schedule(dynamic, 256) if(nets.size() >= 4096)
        for (long i = 0; i < (long)nets.size(); i++)
            for (size_t k = 0; k < nets[i].num_pins; k++) {
                const AIEplace::DefNetPin& p = nets[i].pins[k];
                if (p.first == "PIN") continue;
                std::string comp(p.first);
                auto node = node_id.find(comp);
                if (node == node_id.end()) continue;
                pin_node[first_pin[i] + k] = node->second;
                auto macro = lef.find(master_of.at(comp));
                if (macro == lef.end()) continue;
                auto offset = macro->second.pin_offset.find(std::string(p.second));
                if (offset != macro->second.pin_offset.end()) pin_offset[first_pin[i] + k] = &offset->second;
            }
        std::vector<Pin> net;
        for (size_t i = 0; i < nets.size(); i++) {
            net.clear();
            for (size_t k = 0; k < nets[i].num_pins; k++) {
                const AIEplace::DefNetPin& p = nets[i].pins[k];
                Pin pin;
                std::array<float, 2> offset = {0.0f, 0.0f};
                if (p.first == "PIN") {
                    auto it = io_pin_node.find(std::string(p.second));
                    if (it == io_pin_node.end()) {
                        it = io_pin_node.emplace(std::string(p.second), (int)nl.movable.size()).first;
                        nl.movable.push_back(0);
                        nl.is_macro.push_back(0);
                    }
                    pin.node = it->second;
                } else {
                    pin.node = pin_node[first_pin[i] + k];
                    if (pin.node < 0) return fail("unknown comp " + std::string(p.first));
                    if (!pin_offset[first_pin[i] + k])
                        return fail("no LEF pin " + master_of.at(std::string(p.first)) + "/" + std::string(p.second));
                    offset = *pin_offset[first_pin[i] + k];
                }
                for (int axis = 0; axis < 2; axis++) pin.offset_key[axis] = dict[axis].key(offset[axis], nl.offset_value[axis]);
                net.push_back(pin);
            }
            keep(net);
        }
    }

    // The Bookshelf macro rule needs every node's height, so it runs once the reads are done. Meow.
    void finish_bookshelf()
    {
        std::map<int, long> height_count;
        for (size_t n = 0; n < height.size(); n++) if (nl.movable[n]) height_count[height[n]]++;
        int row_height = 0; long best = -1;
        for (const auto& entry : height_count) if (entry.second > best) { best = entry.second; row_height = entry.first; }
        nl.is_macro.resize(height.size());
        for (size_t n = 0; n < height.size(); n++) nl.is_macro[n] = height[n] > row_height;
    }

private:
    std::unordered_map<std::string, int> node_id, io_pin_node;
    std::unordered_map<std::string, std::string> master_of;
    std::unordered_map<std::string, LefMacro> lef;
    LefMacro* current_macro = nullptr;
    std::vector<int> height;
    OffsetDict dict[2];

    void keep(const std::vector<Pin>& net)
    {
        if ((int)net.size() >= MIN_NET_DEGREE && (int)net.size() <= IGNORE_NET_DEGREE) nl.nets.push_back(net);
    }
    void fail(const std::string& what) { if (error.empty()) error = what; }
};

inline Netlist native_netlist_or_exit(NetlistSink& sink, bool read_ok, const std::string& what)
{
    if (!read_ok || !sink.error.empty()) {
        fprintf(stderr, "%s: %s\n", what.c_str(), sink.error.empty() ? "read failed (see log)" : sink.error.c_str());
        exit(2);
    }
    return std::move(sink.nl);
}

// Same arguments and result as read_bookshelf. Only .nodes and .nets are read, as there. Meow.
inline Netlist read_bookshelf_native(const std::string& dir, const std::string& name)
{
    NetlistSink sink;
    const std::string suite_dir = dir.substr(0, dir.find_last_of('/'));
    sink.nl.name = suite_dir.substr(suite_dir.find_last_of('/') + 1) + "/" + name;   // mms/ and ispd2005/ share names
    bool ok = AIEplace::readBookshelfFile(dir + "/" + name + ".nodes", sink)
           && AIEplace::readBookshelfFile(dir + "/" + name + ".nets", sink);
    sink.finish_bookshelf();
    return native_netlist_or_exit(sink, ok, dir + "/" + name);
}

// Same arguments and result as read_def: the DEF, plus cells.lef next to it. Meow.
inline Netlist read_def_native(const std::string& path, const std::string& name)
{
    NetlistSink sink;
    sink.nl.name = name;
    bool ok = AIEplace::readLefFile(path.substr(0, path.find_last_of('/')) + "/cells.lef", sink)
           && AIEplace::readDefFile(path, sink);
    return native_netlist_or_exit(sink, ok, path);
}

} // namespace packer

#endif
