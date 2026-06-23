/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

namespace ToolKit
{

  // String Hash Utilities.
  //////////////////////////////////////////

  /**
   * 64 bit hash function for strings.
   * https://github.com/explosion/murmurhash/blob/master/murmurhash/MurmurHash2.cpp#L130
   */
  constexpr uint64_t MurmurHash64A(const void* key, int len, uint64_t seed)
  {
    const uint64_t m          = 0xc6a4a7935bd1e995;
    const int r               = 47;

    uint64_t h                = seed ^ (len * m);

    // Walk the input as bytes so the function stays a true constant expression
    // (a reinterpret_cast on the key pointer would not be allowed in a
    // constexpr context under C++17). Reading 8 bytes individually also makes
    // the hash endian-portable, which is what we want for class-name lookups.
    const unsigned char* data = static_cast<const unsigned char*>(key);
    const unsigned char* end  = data + (len & ~7);

    while (data != end)
    {
      uint64_t k  = uint64_t(data[0]) | (uint64_t(data[1]) << 8) | (uint64_t(data[2]) << 16) |
                    (uint64_t(data[3]) << 24) | (uint64_t(data[4]) << 32) | (uint64_t(data[5]) << 40) |
                    (uint64_t(data[6]) << 48) | (uint64_t(data[7]) << 56);
      data       += 8;

      k          *= m;
      k          ^= k >> r;
      k          *= m;

      h          ^= k;
      h          *= m;
    }

    const unsigned char* data2 = data;

    switch (len & 7)
    {
      case 7:
        h ^= uint64_t(data2[6]) << 48;
      case 6:
        h ^= uint64_t(data2[5]) << 40;
      case 5:
        h ^= uint64_t(data2[4]) << 32;
      case 4:
        h ^= uint64_t(data2[3]) << 24;
      case 3:
        h ^= uint64_t(data2[2]) << 16;
      case 2:
        h ^= uint64_t(data2[1]) << 8;
      case 1:
        h ^= uint64_t(data2[0]);
        h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
  }

  /** Compile-time FNV-1a hash for strings. */
  constexpr uint64_t TKConstexprHash(const char* str, size_t len)
  {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; ++i)
    {
      hash ^= static_cast<uint64_t>(str[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

} // namespace ToolKit