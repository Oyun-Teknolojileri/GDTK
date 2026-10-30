/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "GLProgramCache.h"

#ifdef TK_ANDROID

  #include "TKOpenGL.h"

  #include <ToolKit.h>

  #include <chrono>
#include <cstdio>
  #include <cstring>
  #include <fstream>

namespace ToolKit
{

  namespace
  {
    // The file starts with a small header so a stale or foreign binary is rejected instead of being
    // fed to glProgramBinary, which would fail with no way to tell why.
    constexpr char CacheMagic[4]   = {'T', 'K', 'P', 'B'};
    constexpr uint CacheVersion  = 1;
    constexpr uint HeaderPadding = 0;

    struct CacheHeader
    {
      char magic[4];
      uint version;
      uint64 key;
      uint64 driver;
      uint format;
      uint size;
    };

    /** FNV-1a over a byte range, continued across calls so a hash can span both sources. */
    uint64 HashBytes(uint64 hash, const void* data, size_t size)
    {
      const unsigned char* bytes = static_cast<const unsigned char*>(data);
      for (size_t i = 0; i < size; i++)
      {
        hash ^= (uint64) bytes[i];
        hash *= 1099511628211ull;
      }
      return hash;
    }

    constexpr uint64 HashOffset = 14695981039346656037ull;

    /** Lazily created cache directory under the project's resource root. */
    const String& CacheDirectory()
    {
      static String directory;
      if (directory.empty())
      {
        directory = ConcatPaths({ResourcePath(), "..", "ShaderCache"});
        std::error_code errorCode;
        std::filesystem::create_directories(directory, errorCode);
      }
      return directory;
    }
  } // namespace


  void ReportProgramCacheEvent(const char* message)
  {
    static int counter = 0;
    if (message == nullptr || counter > 256)
    {
      return;
    }
    counter++;

    // A phone's logcat does not always carry the app's lines, so the cache records what it did next
    // to the binaries and that file can be pulled over adb.
    std::ofstream report(ConcatPaths({CacheDirectory(), "report.txt"}), std::ios::app);
    if (report.is_open())
    {
      report << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count()
             << " " << message << "\n";
    }
  }

  uint64 HashDriverIdentity()
  {
    // The GL strings identify the driver well enough for a binary cache: a different GPU, driver
    // family or driver version means the format and the compiled code may differ.
    const char* strings[] = {(const char*) glGetString(GL_VENDOR),
                             (const char*) glGetString(GL_RENDERER),
                             (const char*) glGetString(GL_VERSION)};

    uint64 hash = HashOffset;
    for (const char* text : strings)
    {
      if (text != nullptr)
      {
        hash = HashBytes(hash, text, std::strlen(text));
      }
    }
    return hash;
  }

  String ProgramCachePath(uint64 key)
  {
    char name[32];
    snprintf(name, sizeof(name), "%016llx.bin", (unsigned long long) key);
    return ConcatPaths({CacheDirectory(), name});
  }

  bool ReadProgramBinary(uint64 key, unsigned int& format, std::vector<unsigned char>& binary)
  {
    std::ifstream file(ProgramCachePath(key), std::ios::binary);
    if (!file.is_open())
    {
      ReportProgramCacheEvent("miss (no file)");
      return false;
    }

    CacheHeader header {};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good())
    {
      return false;
    }

    if (std::memcmp(header.magic, CacheMagic, sizeof(CacheMagic)) != 0 ||
        header.version != CacheVersion || header.key != key ||
        header.driver != HashDriverIdentity() || header.size == 0)
    {
      return false;
    }

    binary.resize(header.size);
    file.read(reinterpret_cast<char*>(binary.data()), (std::streamsize) header.size);
    if (file.gcount() != (std::streamsize) header.size)
    {
      binary.clear();
      return false;
    }

    format = header.format;
    ReportProgramCacheEvent("hit");
    return true;
  }

  void WriteProgramBinary(uint64 key, unsigned int format, const void* binary, size_t binarySize)
  {
    if (binary == nullptr || binarySize == 0 || binarySize > (size_t) UINT32_MAX)
    {
      return;
    }

    const String path = ProgramCachePath(key);
    std::error_code errorCode;
    if (std::filesystem::exists(path, errorCode))
    {
      return;
    }

    CacheHeader header {};
    std::memcpy(header.magic, CacheMagic, sizeof(CacheMagic));
    header.version = CacheVersion;
    header.key     = key;
    header.driver  = HashDriverIdentity();
    header.format  = format;
    header.size    = (uint) binarySize;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
      TK_WRN("GLProgramCache: cannot write %s", path.c_str());
      return;
    }

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(static_cast<const char*>(binary), (std::streamsize) binarySize);

    char line[128];
    snprintf(line, sizeof(line), "wrote %u bytes (format %u)", header.size, header.format);
    ReportProgramCacheEvent(line);
  }

  void ClearProgramCache()
  {
    std::error_code errorCode;
    const String& directory = CacheDirectory();
    if (!std::filesystem::exists(directory, errorCode))
    {
      return;
    }

    int removed = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory, errorCode))
    {
      if (entry.is_regular_file() && entry.path().extension() == ".bin")
      {
        std::filesystem::remove(entry.path(), errorCode);
        removed++;
      }
    }

    TK_LOG("GLProgramCache: cleared %d cached programs.", removed);
  }

} // namespace ToolKit

#endif // TK_ANDROID
