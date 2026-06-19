#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <utility>
#include <functional>
#include "Common.h"
#include "Types.h"

namespace AIEPLACE_NAMESPACE {

  template<
      class Key,
      class T,
      class Hash = std::hash<Key>,
      class KeyEqual = std::equal_to<Key>
  >
  class LockableIndexedCollection {
    public:
      using key_type        = Key;
      using mapped_type     = T;
      using size_type       = std::uint32_t;
      using index_type      = size_type;
      using vector_type     = std::vector<T>;
      using index_map_type  = std::unordered_map<Key, index_type, Hash, KeyEqual>;

      LockableIndexedCollection() = default;

      bool is_locked() const noexcept { return locked_; }

      // --- basic queries (always allowed) ---
      size_type size() const noexcept { return static_cast<size_type>(vec_.size()); }
      bool empty() const noexcept { return vec_.empty(); }

      const vector_type& data() const noexcept { return vec_; }
      const index_map_type& index_map() const noexcept { return index_; }

      // --- access by index (read) ---
      const T& at_index(index_type i) const {
        return vec_.at(static_cast<std::size_t>(i));
      }
      const T& operator[](index_type i) const noexcept {
        return vec_.at(static_cast<std::size_t>(i));
      }
      const std::string& name_at(index_type idx) const {
        auto it = vec_.begin() + idx;
        if (it >= vec_.end())
          throw std::out_of_range("index out of range");
        return it->name;
      }

      // --- acess by index (write) ---
      T& at_index(index_type i) {
        ensure_unlocked();
        return vec_.at(static_cast<std::size_t>(i));
      }

      // --- lookup by key ---
      // returns: true if found, fills out idx
      bool find_index(const key_type& key, index_type& idx_out) const {
        auto it = index_.find(key);
        if (it == index_.end())
          return false;
        idx_out = it->second;
        return true;
      }
      // throws std::out_of_range if not found
      index_type index_of(const key_type& key) const {
        auto it = index_.find(key);
        if (it == index_.end())
          throw std::out_of_range("Key not found");
        return it->second;
      }


      // --- insertion (only when unlocked) ---
      // returns index of the new element.
      template<class... Args>
      index_type emplace(Args&&... args) {
        ensure_unlocked();
        mapped_type obj(std::forward<Args>(args)...);

        auto const& name = obj.name;

        auto [it, inserted] = index_.emplace(name, index_type{});
        if (!inserted)
          throw std::runtime_error("Duplicate key in LockableIndexedCollection: " + name);

        index_type idx = static_cast<index_type>(vec_.size());
        vec_.emplace_back(std::move(obj));
        it->second = idx;
        return idx;
      }

      // --- rearranging the vector while unlocked ---
      template<class Compare>
      void sort(Compare comp) {
        ensure_unlocked();

        // sort vector
        std::sort(vec_.begin(), vec_.end(), comp);

        // update index map
        update_index();

      }

      // --- fix the vector into an array ---
      void lock() {
        ensure_unlocked();

        // update index
        update_index();

        locked_ = true;
      }

      // iterators (read-write)
      vector_type::iterator begin() { return vec_.begin(); }
      vector_type::iterator end() { return vec_.end(); }
      vector_type::iterator cbegin() { return vec_.cbegin(); }
      vector_type::iterator cend() { return vec_.cend(); }

      // iterators (read-only)
      vector_type::const_iterator begin() const { return vec_.begin(); }
      vector_type::const_iterator end() const { return vec_.end(); }
      vector_type::const_iterator cbegin() const { return vec_.cbegin(); }
      vector_type::const_iterator cend() const { return vec_.cend(); }

    private:
      void ensure_unlocked() const {
        if (locked_)
          throw std::logic_error("Collection is already finalized and locked");
      }
      void ensure_locked() const {
        if (!locked_)
          throw std::logic_error("Collection is not yet finalized");
      }

      void update_index() {
        for (index_type i = 0; i < static_cast<index_type>(vec_.size()); ++i) {
          const key_type& key = vec_[i].name;

          auto it = index_.find(key);
          if (it != index_.end()) {
            it->second = i;  // update stored index
          } else {
            // TODO: throw error if a key is not yet in the index
          }
        }
      }

      vector_type vec_;
      index_map_type index_;
      bool locked_ = false;

  };

  using ComponentTypeLibrary =
    LockableIndexedCollection<std::string, ComponentType>;
  using ComponentLibrary =
    LockableIndexedCollection<std::string, Component>;
  using NetLibrary =
      LockableIndexedCollection<std::string, Net>;

  std::ostream& operator<<(std::ostream& os, ComponentState s);
  std::ostream& operator<<(std::ostream& os, ComponentKind k);
  std::ostream& printComponentType(std::ostream& os, const ComponentType& ct);
  std::ostream& printComponent(std::ostream& os, const Component& c);
  std::ostream& printNet(std::ostream& os, const Net& n);
  void print_component_type_library(const ComponentTypeLibrary& lib, std::ostream& os = std::cout);
  void print_component_library(const ComponentLibrary& lib, std::ostream& os = std::cout);
  void print_net_library(const std::map<uint16_t, NetLibrary>& lib, std::ostream& os = std::cout);

}
