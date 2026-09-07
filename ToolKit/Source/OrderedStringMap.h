/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file OrderedStringMap.h Header for OrderedStringMap
 */

#include "Types.h"

namespace ToolKit
{

  /**
   * A map that preserves the insertion order of its entries while providing
   * fast lookup by a unique string key. Iterating the map yields the entries
   * in the order they were inserted; entries carry the key as the first
   * element and the stored value as the second element.
   */
  template <typename T>
  class OrderedStringMap
  {
   public:
    typedef std::pair<String, T> Entry;
    typedef std::vector<Entry> Container;
    typedef typename Container::iterator iterator;
    typedef typename Container::const_iterator const_iterator;

    OrderedStringMap() = default;

    bool empty() const { return m_entries.empty(); }
    size_t size() const { return m_entries.size(); }

    void clear()
    {
      m_entries.clear();
      m_lookup.clear();
    }

    iterator begin() { return m_entries.begin(); }
    iterator end() { return m_entries.end(); }
    const_iterator begin() const { return m_entries.begin(); }
    const_iterator end() const { return m_entries.end(); }

    /**
     * Finds the value stored for the given key.
     * @param name Key of the entry.
     * @return Stored value or nullptr when the key does not exist.
     */
    T* Find(const String& name);
    const T* Find(const String& name) const;

    /**
     * Checks if an entry with the given key exists.
     * @param name Key of the entry.
     * @return True when the key is present.
     */
    bool Contains(const String& name) const { return Find(name) != nullptr; }

    /**
     * Appends an entry at the end unless its key already exists.
     * @param name Key of the entry.
     * @param value Value to store.
     * @return False when the key is already present.
     */
    bool Insert(const String& name, const T& value);
    bool Insert(const String& name, T&& value);

    /**
     * Removes the entry with the given key. The order of the remaining
     * entries is preserved.
     * @param name Key of the entry.
     * @return True when an entry was removed.
     */
    bool Erase(const String& name);

    /**
     * Returns the entry at the given insertion position. Keeps vector style
     * indexing for callers that iterate the rows by index.
     * @param index Position of the entry.
     * @return The entry at the given position.
     */
    Entry& operator[](size_t index) { return m_entries[index]; }
    const Entry& operator[](size_t index) const { return m_entries[index]; }

    /**
     * Renames the key of an entry in place. The insertion order is preserved
     * and the lookup is updated.
     * @param oldName Current key of the entry.
     * @param newName New unique key.
     * @return False when the old key does not exist or the new key is taken.
     */
    bool Rename(const String& oldName, const String& newName)
    {
      if (oldName == newName)
      {
        return true;
      }

      auto it = m_lookup.find(oldName);
      if (it == m_lookup.end() || m_lookup.find(newName) != m_lookup.end())
      {
        return false;
      }

      size_t index = it->second;
      m_entries[index].first = newName;
      m_lookup.erase(it);
      m_lookup[newName] = index;
      return true;
    }

   private:
    void RebuildLookup()
    {
      m_lookup.clear();
      for (size_t i = 0; i < m_entries.size(); i++)
      {
        m_lookup[m_entries[i].first] = i;
      }
    }

    Container m_entries;
    std::unordered_map<String, size_t> m_lookup;
  };

  template <typename T>
  T* OrderedStringMap<T>::Find(const String& name)
  {
    auto it = m_lookup.find(name);
    if (it == m_lookup.end())
    {
      return nullptr;
    }
    return &m_entries[it->second].second;
  }

  template <typename T>
  const T* OrderedStringMap<T>::Find(const String& name) const
  {
    auto it = m_lookup.find(name);
    if (it == m_lookup.end())
    {
      return nullptr;
    }
    return &m_entries[it->second].second;
  }

  template <typename T>
  bool OrderedStringMap<T>::Insert(const String& name, const T& value)
  {
    if (Contains(name))
    {
      return false;
    }
    m_lookup[name] = m_entries.size();
    m_entries.push_back(Entry(name, value));
    return true;
  }

  template <typename T>
  bool OrderedStringMap<T>::Insert(const String& name, T&& value)
  {
    if (Contains(name))
    {
      return false;
    }
    m_lookup[name] = m_entries.size();
    m_entries.push_back(Entry(name, std::move(value)));
    return true;
  }

  template <typename T>
  bool OrderedStringMap<T>::Erase(const String& name)
  {
    auto it = m_lookup.find(name);
    if (it == m_lookup.end())
    {
      return false;
    }

    m_entries.erase(m_entries.begin() + it->second);
    RebuildLookup();
    return true;
  }

  /**
   * Checks if two ordered maps store the same set of keys.
   */
  template <typename T>
  bool HaveSameKeys(const OrderedStringMap<T>& map1, const OrderedStringMap<T>& map2)
  {
    if (map1.size() != map2.size())
    {
      return false;
    }

    for (const auto& entry : map1)
    {
      if (!map2.Contains(entry.first))
      {
        return false;
      }
    }

    return true;
  }

  /** Animation record tracks kept in their insertion order. */
  typedef OrderedStringMap<AnimRecordPtr> AnimRecordPtrMap;

} // namespace ToolKit
