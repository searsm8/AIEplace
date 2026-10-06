/**
 * @file NameIndex.h
 * @brief A flat open-addressing name -> T* table for the parse (TODO #43). The parse looks a
 *        node up by name once per net pin -- ~10 M lookups on the largest designs -- and with a
 *        node-based table every lookup is several dependent cache misses. Here a probe is one
 *        miss on the slot array, plus reading the name on the object the caller touches next
 *        anyway. Keys are the objects' own names (getName() must stay put while indexed).
 *        Insert keeps the FIRST object under a name, like std::map::emplace. Meow.
 */
#pragma once
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

template <typename T>
class NameIndex
{
public:
    /// @return false (and leaves the table unchanged) if the name is already present.
    bool insert(T* object)
    {
        if (2 * (m_size + 1) > m_slots.size()) grow();
        std::string_view name = object->getName();
        uint64_t hash = hashOf(name);
        for (size_t i = hash & m_mask; ; i = (i + 1) & m_mask) {
            Slot& slot = m_slots[i];
            if (!slot.object) { slot = {hash, object}; m_size++; return true; }
            if (slot.hash == hash && std::string_view(slot.object->getName()) == name) return false;
        }
    }

    T* find(std::string_view name) const { return find(name, hashOf(name)); }

    // Split lookup, so a caller with several names in hand can start every slot's cache miss
    // before waiting on any of them: hashOf() each, prefetch() each, then find(name, hash). Meow.
    static uint64_t hashOf(std::string_view name) { return std::hash<std::string_view>()(name); }
    void prefetch(uint64_t hash) const { if (!m_slots.empty()) __builtin_prefetch(&m_slots[hash & m_mask]); }
    // Second stage, once the slot is in cache: prefetch the object find() will compare against.
    void prefetchObject(uint64_t hash) const
    {
        if (!m_slots.empty() && m_slots[hash & m_mask].object) __builtin_prefetch(m_slots[hash & m_mask].object);
    }

    T* find(std::string_view name, uint64_t hash) const
    {
        if (m_slots.empty()) return nullptr;
        for (size_t i = hash & m_mask; ; i = (i + 1) & m_mask) {
            const Slot& slot = m_slots[i];
            if (!slot.object) return nullptr;
            if (slot.hash == hash && std::string_view(slot.object->getName()) == name) return slot.object;
        }
    }

    void clear() { m_slots = {}; m_size = 0; m_mask = 0; }

private:
    struct Slot { uint64_t hash; T* object; };
    std::vector<Slot> m_slots;
    size_t m_size = 0, m_mask = 0;

    // Rehash from the stored hashes alone: growing never touches the objects. Meow.
    void grow()
    {
        std::vector<Slot> old = std::move(m_slots);
        m_slots.assign(old.empty() ? 1024 : 2 * old.size(), Slot{0, nullptr});
        m_mask = m_slots.size() - 1;
        for (const Slot& slot : old) {
            if (!slot.object) continue;
            size_t i = slot.hash & m_mask;
            while (m_slots[i].object) i = (i + 1) & m_mask;
            m_slots[i] = slot;
        }
    }
};
