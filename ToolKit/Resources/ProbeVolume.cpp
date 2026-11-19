/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "ProbeVolume.h"

#include "FileManager.h"
#include "Logger.h"
#include "ToolKit.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace ToolKit
{
  namespace
  {
    constexpr char ProbeVolumeMagic[4] = {'P', 'G', 'I', '1'};

    template <typename T>
    void WritePod(std::ofstream& stream, const T& value)
    {
      stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    /**
     * Cursor over the bytes of a probe volume.
     *
     * The file is read whole rather than streamed, because the same bytes have to come out of the
     * resource pak and out of a loose file, and only the buffer form is common to both.
     */
    struct ByteReader
    {
      const ubyte* data = nullptr;
      size_t size       = 0;
      size_t offset     = 0;

      bool Read(void* out, size_t bytes)
      {
        if (data == nullptr || offset + bytes > size)
        {
          return false;
        }

        std::memcpy(out, data + offset, bytes);
        offset += bytes;
        return true;
      }

      template <typename T>
      bool Pod(T& value)
      {
        return Read(&value, sizeof(T));
      }
    };
  } // namespace

  bool SaveProbeVolume(const String& path, const ProbeVolumeData& data)
  {
    const uint coefficientCount = data.CoefficientCount();
    const size_t probeCount       = data.ProbeCount();

    if (probeCount == 0)
    {
      TK_ERR("Probe volume '%s' has an empty grid.", path.c_str());
      return false;
    }

    if (data.coefficients.size() != probeCount * coefficientCount * 3)
    {
      TK_ERR("Probe volume '%s' holds %zu coefficients, expected %zu.",
             path.c_str(),
             data.coefficients.size(),
             probeCount * coefficientCount * 3);
      return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
      // A tool that has to be told to create its own output folder is a tool that fails on its
      // first run, so the folder is created here and the open is retried once.
      std::error_code error;
      const std::filesystem::path parent = std::filesystem::path(path).parent_path();
      if (!parent.empty())
      {
        std::filesystem::create_directories(parent, error);
        stream.open(path, std::ios::binary | std::ios::trunc);
      }
    }

    if (!stream.is_open())
    {
      TK_ERR("Probe volume '%s' can not be opened for writing.", path.c_str());
      return false;
    }

    stream.write(ProbeVolumeMagic, sizeof(ProbeVolumeMagic));

    const uint version = ProbeVolumeData::CurrentVersion;
    WritePod(stream, version);

    const uint resX = data.resolution.x;
    const uint resY = data.resolution.y;
    const uint resZ = data.resolution.z;
    WritePod(stream, resX);
    WritePod(stream, resY);
    WritePod(stream, resZ);

    const uint quality = coefficientCount;
    WritePod(stream, quality);

    stream.write(reinterpret_cast<const char*>(glm::value_ptr(data.transform)), sizeof(float) * 16);
    stream.write(reinterpret_cast<const char*>(glm::value_ptr(data.boxMin)), sizeof(float) * 3);
    stream.write(reinterpret_cast<const char*>(glm::value_ptr(data.boxMax)), sizeof(float) * 3);

    const uint64 storedProbes = probeCount;
    WritePod(stream, storedProbes);

    stream.write(reinterpret_cast<const char*>(data.coefficients.data()),
                 (std::streamsize) (data.coefficients.size() * sizeof(float)));

    // A probe with no validity entry is valid, so a compact file does not have to carry one.
    if (data.valid.size() == probeCount)
    {
      stream.write(reinterpret_cast<const char*>(data.valid.data()), (std::streamsize) probeCount);
    }
    else
    {
      std::vector<uint8> allValid(probeCount, 1);
      stream.write(reinterpret_cast<const char*>(allValid.data()), (std::streamsize) probeCount);
    }

    if (!stream.good())
    {
      TK_ERR("Probe volume '%s' failed while writing.", path.c_str());
      return false;
    }

    return true;
  }

  bool LoadProbeVolume(const String& path, ProbeVolumeData& data)
  {
    std::vector<ubyte> bytes;
    {
      // Through the FileManager when the engine is up: it is the only reader that knows about the
      // resource pak, and it falls back to the loose file on its own. A published build carries
      // its .pgi inside the pak, where an ifstream would never find it.
      bool read = false;
      if (Main::GetInstance_noexcep() != nullptr)
      {
        read = GetFileManager()->ReadFileBuffer(path, bytes);
      }

      if (!read)
      {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream.is_open())
        {
          TK_ERR("Probe volume '%s' does not exist.", path.c_str());
          return false;
        }

        const std::streamoff size = stream.tellg();
        if (size <= 0)
        {
          TK_ERR("Probe volume '%s' is empty.", path.c_str());
          return false;
        }

        bytes.resize((size_t) size);
        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize) size);
        if (!stream.good())
        {
          TK_ERR("Probe volume '%s' failed while reading.", path.c_str());
          return false;
        }
      }
    }

    ByteReader reader {bytes.data(), bytes.size(), 0};

    char magic[sizeof(ProbeVolumeMagic)] = {0};
    if (!reader.Read(magic, sizeof(magic)) || std::memcmp(magic, ProbeVolumeMagic, sizeof(magic)) != 0)
    {
      TK_ERR("Probe volume '%s' is not a .pgi file.", path.c_str());
      return false;
    }

    uint version = 0;
    if (!reader.Pod(version) || version != ProbeVolumeData::CurrentVersion)
    {
      TK_ERR("Probe volume '%s' has version %u, this build reads version %u.",
             path.c_str(),
             version,
             ProbeVolumeData::CurrentVersion);
      return false;
    }

    uint resX = 0;
    uint resY = 0;
    uint resZ = 0;
    uint quality = 0;
    if (!reader.Pod(resX) || !reader.Pod(resY) || !reader.Pod(resZ) || !reader.Pod(quality))
    {
      TK_ERR("Probe volume '%s' ended in its header.", path.c_str());
      return false;
    }

    if (resX == 0 || resY == 0 || resZ == 0)
    {
      TK_ERR("Probe volume '%s' has an empty resolution %ux%ux%u.", path.c_str(), resX, resY, resZ);
      return false;
    }

    if (quality != (uint) ProbeVolumeQuality::L1 && quality != (uint) ProbeVolumeQuality::L2)
    {
      TK_ERR("Probe volume '%s' has an unknown quality %u.", path.c_str(), quality);
      return false;
    }

    data.version    = version;
    data.resolution = UVec3(resX, resY, resZ);
    data.quality    = (ProbeVolumeQuality) quality;

    if (!reader.Read(glm::value_ptr(data.transform), sizeof(float) * 16) ||
        !reader.Read(glm::value_ptr(data.boxMin), sizeof(float) * 3) ||
        !reader.Read(glm::value_ptr(data.boxMax), sizeof(float) * 3))
    {
      TK_ERR("Probe volume '%s' ended in its transform.", path.c_str());
      return false;
    }

    uint64 storedProbes = 0;
    if (!reader.Pod(storedProbes) || storedProbes != (uint64) data.ProbeCount())
    {
      TK_ERR("Probe volume '%s' declares %llu probes for a %ux%ux%u grid.",
             path.c_str(),
             (unsigned long long) storedProbes,
             resX,
             resY,
             resZ);
      return false;
    }

    const size_t coefficientCount = data.ProbeCount() * data.CoefficientCount() * 3;
    data.coefficients.resize(coefficientCount);
    data.valid.resize(data.ProbeCount());

    if (!reader.Read(data.coefficients.data(), coefficientCount * sizeof(float)) ||
        !reader.Read(data.valid.data(), data.ProbeCount()))
    {
      TK_ERR("Probe volume '%s' ended in its payload.", path.c_str());
      return false;
    }

    return true;
  }

} // namespace ToolKit
