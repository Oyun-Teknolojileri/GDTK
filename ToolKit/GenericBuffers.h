/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Types.h"

namespace ToolKit
{

  // CacheItem
  //////////////////////////////////////////

  /** Single item for generic cache. Each item struct must drive from this and store data suitable to std140 layout. */
  struct TK_API CacheItem
  {
    ObjectId id             = NullHandle; //!< Unique id of the item.
    int version             = -1;    //!< If version doesn't match the with cached item, forces a cache invalidation.
    bool isValid            = false; //!< States if the current state of the item is valid.

    /** Returns the data that will be passed to gpu. */
    virtual void* GetData() = 0;

    /** Set the cache status to invalid. */
    void Invalidate() { isValid = false; }

    /** Owner class calls this function when updates the item. Version increments to causes gpu cache update. */
    void Validate()
    {
      version++;
      isValid = true;
    }
  };

  // LRUCache
  //////////////////////////////////////////

  /** Interface for cacheable object. Cacheable classes must derive from this interface such as Material and Light. */
  class TK_API ICacheable
  {
   public:
    /** Suppose to return the cache item of the class implementing this interface. */
    virtual const CacheItem& GetCacheItem() = 0;

    /** Suppose to invalidate the CacheItem without needing handle to data. */
    virtual void InvalidateCacheItem()      = 0;
  };

  /** LRU based generic cache. T must be a type derived from CacheItem. Size is in bytes. */
  template <typename T, uint64 ItemSize>
  class TK_API LRUCache
  {
   public:
    LRUCache(uint64 byteSize) : m_cacheSize(byteSize) { m_data = new byte[m_cacheSize]; }

    virtual ~LRUCache() { SafeDelArray(m_data); }

    LRUCache(const LRUCache&)            = delete; //!< Prevent copy.
    LRUCache& operator=(const LRUCache&) = delete; //!< Prevent assignment.

    /** Adds or updates a cache item, invalidates the cache if needed. Returns index of the item. */
    int AddOrUpdateItem(const T& item)
    {
      // If item exist, check if update is needed.
      auto itemItr = m_cacheMap.find(item.id);
      if (itemItr != m_cacheMap.end())
      {
        // Check versions
        T& CacheItem = *itemItr->second;
        if (CacheItem.version != item.version)
        {
          // Set the new item data.
          m_lruCacheList.erase(itemItr->second);
          m_lruCacheList.push_front(item);
          m_cacheMap[item.id] = m_lruCacheList.begin();

          m_isValid           = false;
          return 0; // Material is the first item in the cache.
        }
      }
      else
      {
        // Check if there is enough space
        uint64 usedSize = ConsumedSize();
        if (usedSize + ItemSize > m_cacheSize)
        {
          // Not enough space, drop the last item
          T& lastItem = m_lruCacheList.back();
          m_cacheMap.erase(lastItem.id);
          m_lruCacheList.pop_back();
        }

        // Insert the new item.
        m_lruCacheList.push_front(item);
        m_cacheMap[item.id] = m_lruCacheList.begin();

        m_isValid           = false;
        return 0; // Material is the first item in the cache.
      }

      return (int) std::distance(m_lruCacheList.begin(), itemItr->second);
    }

    /**
     * Returns the corresponding indexes of items in the cache.
     * Consider only the first n if its provided.
     * Call after buffer is mapped. Otherwise indexes will be invalid.
     */
    IntArray LookUp(const IDArray& items, int n = -1)
    {
      assert(m_isValid && "Map the cache first, buffer is invalid.");

      IntArray ids;
      ids.reserve(items.size());

      if (n == -1)
      {
        n = (int) items.size();
      }
      else
      {
        n = glm::min((int) items.size(), n);
      }

      for (int i = 0; i < n; i++)
      {
        ObjectId id  = items[i];
        auto itemItr = m_cacheMap.find(id);
        if (itemItr != m_cacheMap.end())
        {
          ids.push_back((int) std::distance(m_lruCacheList.begin(), itemItr->second));
        }
      }

      return ids;
    }

    /** Resets the cache. Flush all the items. */
    void Reset()
    {
      m_cacheMap.clear();
      m_lruCacheList.clear();
      memset(m_data, 0, m_cacheSize);
      m_isValid = false;
    }

    /** Returns used size of the cache in bytes. */
    uint64 ConsumedSize() const { return ItemSize * m_lruCacheList.size(); }

    /**
     * Maps the Items to cache data in LRU.
     * Calls the update function with current cache and size if cache is invalidated.
     * returns true if mapping is performed in case of invalidation.
     */
    bool Map(std::function<void(const void* data, uint64 size)> updateFn = nullptr)
    {
      bool mapped = false;
      if (!m_isValid)
      {
        int i = 0;
        for (auto& item : m_lruCacheList)
        {
          void* data = item.GetData();
          memcpy(static_cast<byte*>(m_data) + i * ItemSize, data, ItemSize);
          i++;
        }

        if (updateFn != nullptr)
        {
          updateFn(m_data, m_cacheSize);
        }

        m_isValid = true;
        mapped    = true;
      }

      return mapped;
    }

   public:
    /** Size of the cache in bytes. */
    const uint64 m_cacheSize;

   private:
    typedef std::list<T> CacheList;
    typedef std::unordered_map<ObjectId, typename CacheList::iterator> CacheMap;

    /** Storage of items by id and list iterator. This map is used to see if an get its iterator in m_cacheList. */
    CacheMap m_cacheMap;
    /** List of item in the cache stored from least recently used to last.  */
    CacheList m_lruCacheList;
    /** States if a gpu map is needed. */
    int m_isValid = false;
    /** The full data that will be passed to gpu. */
    void* m_data  = nullptr;
  };

  // StructBuffer
  //////////////////////////////////////////

  /** Generic buffer that holds array of struct and maps it to underlaying buffer. */
  template <typename DataLayout, int Slot = -1>
  class TK_API StructBuffer
  {
   public:
    /** Map cpu side data to underlying buffer object via mapFn callback. */
    void Map(std::function<void(void* data, uint64 size)> mapFn)
    {
      if (m_data.size() > 0)
      {
        mapFn(m_data.data(), m_data.size() * sizeof(DataLayout));
      }
    }

    void Push(const DataLayout& data) { m_data.push_back(data); }

    void Push(DataLayout&& data) { m_data.push_back(std::move(data)); }

    void Clear() { m_data.clear(); }

    void Allocate(int size)
    {
      m_data.reserve(size);
      m_data.resize(size);
    }

   protected:
    std::vector<DataLayout> m_data;
  };

} // namespace ToolKit