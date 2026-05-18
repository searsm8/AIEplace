#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <utility>
#include <functional>
#include "Common.h"
#include "Component.h"

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
      using vector_type     = std::vector<std::pair<Key,T>>;
      using array_type      = std::unique_ptr<T[]>;
      using index_map_type  = std::unordered_map<Key, index_type, Hash, KeyEqual>;
      using names_map_type  = std::map<index_type, Key>;
      using const_iterator  = typename vector_type::const_iterator;

      LockableIndexedCollection() = default;

      bool is_locked() const noexcept { return locked_; }

      // --- basic queries (always allowed) ---
      size_type size() const noexcept { return is_locked() ? static_cast<size_type>(size_) : static_cast<size_type>(vec_.size()); }
      bool empty() const noexcept { return is_locked() ? size_ == 0 : vec_.empty(); }

      const vector_type& items() const noexcept { return vec_; }
      const array_type& data() const noexcept { return arr_; }
      const index_map_type& index_map() const noexcept { return index_; }
      const names_map_type& names_map() const noexcept { return names_; }

      // --- access by index (read) ---
      const T& at_index(index_type i) const {
        if(is_locked()) {
          return arr_[static_cast<std::size_t>(i)];
        }
        return vec_.at(static_cast<std::size_t>(i)).second;
      }
      const T& operator[](index_type i) const noexcept {
        if(is_locked()) {
          return arr_[static_cast<std::size_t>(i)];
        }
        return vec_.at(static_cast<std::size_t>(i)).second;
      }
      const std::string& name_at(index_type idx) const {
        auto it = names_.find(idx);
        if (it == names_.end())
          throw std::out_of_range("index not found in names_");
        return it->second;
      }

      // --- acess by index (write) ---
      T& at_index(index_type i) {
        ensure_unlocked();
        return vec_.at(static_cast<std::size_t>(i)).second;
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
      index_type emplace(const key_type& key, Args&&... args) {
        ensure_unlocked();
        auto [it, inserted] = index_.emplace(key, index_type{});
        if (!inserted)
          throw std::runtime_error("Duplicate key in LockableIndexedCollection");

        index_type idx = static_cast<index_type>(vec_.size());
        vec_.emplace_back(std::make_pair(key, mapped_type(std::forward<Args>(args)...)));
        names_.emplace(idx, key);
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

        // create array
        fix_vector_into_array();
        build_names_from_index();

        locked_ = true;
      }

      // iterators (read-only)
      const_iterator begin() const noexcept { return vec_.begin(); }
      const_iterator end() const noexcept { return vec_.end(); }
      const_iterator cbegin() const noexcept { return vec_.cbegin(); }
      const_iterator cend() const noexcept { return vec_.cend(); }

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
          const key_type& key = vec_[i].first;

          auto it = index_.find(key);
          if (it != index_.end()) {
            it->second = i;  // update stored index
          } else {
            // TODO: throw error if a key is not yet in the index
          }
        }
      }

      void fix_vector_into_array() {
        size_ = vec_.size();
        arr_ = std::make_unique<mapped_type[]>(size_);

        // copy data from vector to array
        std::transform(vec_.begin(), vec_.end(), arr_.get(), [](const auto& p) { return p.second; });

        // clean up vector
        vec_.clear();
        vec_.shrink_to_fit();
      }

      void build_names_from_index() {
        names_.clear();

        for (const auto& [name, idx] : index_) {
          names_[idx] = name;
        }
      }

      size_type size_;
      vector_type vec_;
      array_type arr_;
      index_map_type index_;
      names_map_type names_;
      bool locked_ = false;

  };

  using ComponentTypeLibrary =
    LockableIndexedCollection<std::string, ComponentType>;
  using ComponentLibrary =
    LockableIndexedCollection<std::string, Component>;
  //using NetLibrary =
  //    LockableIndexedCollection<std::string, Net>;

  std::ostream& operator<<(std::ostream& os, ComponentState s);
  std::ostream& operator<<(std::ostream& os, ComponentKind k);
  std::ostream& printComponentType(std::ostream& os, const std::string& name, const ComponentType& ct);
  void print_component_type_library(const ComponentTypeLibrary& lib, std::ostream& os = std::cout);
  void print_component_library(const ComponentLibrary& lib, std::ostream& os = std::cout);

}
