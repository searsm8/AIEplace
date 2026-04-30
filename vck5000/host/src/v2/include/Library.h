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
      using vector_type     = std::vector<T>;
      using map_type        = std::unordered_map<Key, index_type, Hash, KeyEqual>;
      using const_iterator  = typename vector_type::const_iterator;
  
      LockableIndexedCollection() = default;
  
      bool is_locked() const noexcept { return locked_; }
      void lock() noexcept { locked_ = true; }
  
      // --- basic queries (always allowed) ---
      size_type size() const noexcept { return static_cast<size_type>(items_.size()); }
      bool empty() const noexcept { return items_.empty(); }
  
      const vector_type& data() const noexcept { return items_; }
      const map_type& index_map() const noexcept { return index_; }
  
      // --- access by index (read) ---
      const T& at_index(index_type i) const {
          return items_.at(static_cast<std::size_t>(i));
      }
      const T& operator[](index_type i) const noexcept {
          return items_[static_cast<std::size_t>(i)];
      }
  
      // --- access by index (write; only when unlocked) ---
      T& at_index(index_type i) {
          ensure_unlocked();
          return items_.at(static_cast<std::size_t>(i));
      }
      T& operator[](index_type i) noexcept {
          // still only safe to use when unlocked from a logical standpoint
          // (we don't enforce here to keep it cheap; you can add ensure_unlocked()
          // if you want runtime checks)
          ensure_unlocked();
          return items_[static_cast<std::size_t>(i)];
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
  
      // returns pointer to element or nullptr
      const T* find(const key_type& key) const {
          auto it = index_.find(key);
          if (it == index_.end())
              return nullptr;
          return &items_[it->second];
      }
  
      // --- insertion (only when unlocked) ---
      // returns index of the new element.
      index_type add(const key_type& key, const T& value) {
          ensure_unlocked();
          auto [it, inserted] = index_.emplace(key, index_type{});
          if (!inserted)
              throw std::runtime_error("Duplicate key in LockableIndexedCollection");
  
          index_type idx = static_cast<index_type>(items_.size());
          items_.push_back(value);
          it->second = idx;
          return idx;
      }
  
      index_type add(const key_type& key, T&& value) {
          ensure_unlocked();
          auto [it, inserted] = index_.emplace(key, index_type{});
          if (!inserted)
              throw std::runtime_error("Duplicate key in LockableIndexedCollection");
  
          index_type idx = static_cast<index_type>(items_.size());
          items_.push_back(std::move(value));
          it->second = idx;
          return idx;
      }
  
      template<class... Args>
      index_type emplace(const key_type& key, Args&&... args) {
          ensure_unlocked();
          auto [it, inserted] = index_.emplace(key, index_type{});
          if (!inserted)
              throw std::runtime_error("Duplicate key in LockableIndexedCollection");
  
          index_type idx = static_cast<index_type>(items_.size());
          items_.emplace_back(std::forward<Args>(args)...);
          it->second = idx;
          return idx;
      }
  
      // --- rearranging the vector while unlocked ---
      // This allows e.g. sorting by area, width, etc., and will
      // recompute the indices so name -> index stays correct.
      template<class Compare>
      void sort(Compare comp) {
          ensure_unlocked();
          // build a permutation of indices, sort permutation by comp, then reorder
          const auto n = items_.size();
          std::vector<std::size_t> perm(n);
          for (std::size_t i = 0; i < n; ++i) perm[i] = i;
  
          std::sort(perm.begin(), perm.end(),
                    [&](std::size_t a, std::size_t b) {
                        return comp(items_[a], items_[b]);
                    });
  
          apply_permutation(perm);
          rebuild_index_map();
      }
  
      // You can also provide a generic "reorder" given a permutation
      void reorder(const std::vector<index_type>& new_order) {
          ensure_unlocked();
          if (new_order.size() != items_.size())
              throw std::runtime_error("Permutation size mismatch");
  
          std::vector<std::size_t> perm(new_order.size());
          for (std::size_t i = 0; i < new_order.size(); ++i) {
              perm[i] = new_order[i];
          }
          apply_permutation(perm);
          rebuild_index_map();
      }
  
      // iterators (read-only)
      const_iterator begin() const noexcept { return items_.begin(); }
      const_iterator end() const noexcept { return items_.end(); }
      const_iterator cbegin() const noexcept { return items_.cbegin(); }
      const_iterator cend() const noexcept { return items_.cend(); }
  
  private:
      void ensure_unlocked() const {
          if (locked_)
              throw std::logic_error("Collection is locked");
      }
  
      void rebuild_index_map() {
          // assumes keys inside T are stable, so we need an external way
          // to recover the key. For your use, you control how key is stored.
          // Here we assume key is the name stored externally in a parallel
          // structure or T has a "name" member. For more generality you can
          // pass a function/lambda into this class to extract a key from T,
          // or store the keys separately.
          // For ComponentType, we will store keys only in the map, not in T,
          // or we re-use T::name as key.
      }
  
      // helper: apply permutation in-place to items_
      void apply_permutation(const std::vector<std::size_t>& perm) {
          const std::size_t n = items_.size();
          std::vector<bool> done(n, false);
          for (std::size_t i = 0; i < n; ++i) {
              if (done[i]) continue;
              std::size_t j = i;
              while (!done[j]) {
                  done[j] = true;
                  std::size_t k = perm[j];
                  if (k == j) break;
                  std::swap(items_[j], items_[k]);
                  j = k;
              }
          }
      }
  
      vector_type items_;
      map_type index_;
      bool locked_ = false;
  };

  using ComponentTypeLibrary =
      LockableIndexedCollection<std::string, ComponentType>;
  using ComponentLibrary =
      LockableIndexedCollection<std::string, Component>;
  //using NetlistLibrary =
  //    LockableIndexedCollection<std::string, Netlist>;

  std::ostream& operator<<(std::ostream& os, ComponentState s);
  std::ostream& operator<<(std::ostream& os, ComponentKind k);
  std::ostream& printComponentType(std::ostream& os, const std::string& name, const ComponentType& ct);
  void print_component_type_library(const ComponentTypeLibrary& lib, std::ostream& os = std::cout);

}
