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

    /** The mask stores one bit per probe: bit `probe % 8` of byte `probe / 8`. */
    inline size_t MaskByteOf(size_t probe) { return probe / 8; }
    inline uint8 MaskBitOf(size_t probe) { return (uint8) (1u << (probe % 8)); }

    /** How far the fill has to walk at most: the grid's longest axis reaches every probe. */
    inline int FillSearchRadius(const UVec3& resolution)
    {
      return (int) glm::max(glm::max(resolution.x, resolution.y), resolution.z);
    }
  } // namespace

  ProbeVolumeFill FillUntracedProbes(ProbeVolumeData& data)
  {
    ProbeVolumeFill result {};

    const UVec3 resolution = data.resolution;
    const size_t probeCount = data.ProbeCount();
    const size_t stride     = (size_t) data.CoefficientCount() * 3;

    if (probeCount == 0 || data.valid.size() != probeCount ||
        data.coefficients.size() != probeCount * stride)
    {
      return result;
    }

    // Nothing to copy from. The search would expand to the far corner and find nothing for every
    // single probe, which on a large grid is hours of walking for an empty answer.
    if (data.TracedCount() == 0)
    {
      result.unfillable = (uint) probeCount;
      return result;
    }

    const int maxRadius = FillSearchRadius(resolution);
    float* coefficients = data.coefficients.data();

    for (uint z = 0; z < resolution.z; ++z)
    {
      for (uint y = 0; y < resolution.y; ++y)
      {
        for (uint x = 0; x < resolution.x; ++x)
        {
          const size_t target = ((size_t) z * resolution.y + y) * resolution.x + x;
          if (data.valid[target])
          {
            continue;
          }

          bool filled = false;
          for (int radius = 1; radius <= maxRadius && !filled; ++radius)
          {
            for (int dz = -radius; dz <= radius && !filled; ++dz)
            {
              for (int dy = -radius; dy <= radius && !filled; ++dy)
              {
                for (int dx = -radius; dx <= radius && !filled; ++dx)
                {
                  // Only the shell at this radius, the inside was searched already.
                  const bool onShell = dx == radius || dx == -radius || dy == radius ||
                                       dy == -radius || dz == radius || dz == -radius;
                  if (!onShell)
                  {
                    continue;
                  }

                  const int sx = (int) x + dx;
                  const int sy = (int) y + dy;
                  const int sz = (int) z + dz;
                  if (sx < 0 || sy < 0 || sz < 0 || sx >= (int) resolution.x ||
                      sy >= (int) resolution.y || sz >= (int) resolution.z)
                  {
                    continue;
                  }

                  const size_t source = ((size_t) sz * resolution.y + sy) * resolution.x + sx;
                  if (!data.valid[source])
                  {
                    continue;
                  }

                  std::memcpy(&coefficients[target * stride],
                              &coefficients[source * stride],
                              stride * sizeof(float));
                  filled = true;
                }
              }
            }
          }

          if (filled)
          {
            ++result.filled;
          }
          else
          {
            ++result.unfillable;
          }
        }
      }
    }

    return result;
  }

  bool SaveProbeVolume(const String& path, const ProbeVolumeData& data)
  {
    const uint coefficientCount = data.CoefficientCount();
    const size_t probeCount     = data.ProbeCount();
    const size_t stride         = (size_t) coefficientCount * 3;

    if (probeCount == 0)
    {
      TK_ERR("Probe volume '%s' has an empty grid.", path.c_str());
      return false;
    }

    if (data.coefficients.size() != probeCount * stride)
    {
      TK_ERR("Probe volume '%s' holds %zu coefficients, expected %zu.",
             path.c_str(),
             data.coefficients.size(),
             probeCount * stride);
      return false;
    }

    // A volume without a mask marks every probe traced, so a caller that never ran a sparse bake
    // writes the whole grid - the same payload version 1 carried, only with a packed mask.
    const bool masked = data.valid.size() == probeCount;
    const size_t tracedCount = data.TracedCount();

    std::vector<uint8> mask(data.MaskBytes(), 0);
    std::vector<float> packed(tracedCount * stride);
    size_t written = 0;

    for (size_t probe = 0; probe < probeCount; ++probe)
    {
      if (masked && data.valid[probe] == 0)
      {
        continue;
      }

      mask[MaskByteOf(probe)] |= MaskBitOf(probe);
      std::memcpy(&packed[written],
                  &data.coefficients[probe * stride],
                  stride * sizeof(float));
      written += stride;
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

    const uint64 tracedProbes = tracedCount;
    WritePod(stream, tracedProbes);

    stream.write(reinterpret_cast<const char*>(mask.data()), (std::streamsize) mask.size());
    stream.write(reinterpret_cast<const char*>(packed.data()),
                 (std::streamsize) (packed.size() * sizeof(float)));

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

    // A layout bump does not invalidate the older files: a dense payload is still a volume, and
    // the fill below rests on the mask either way.
    uint version = 0;
    if (!reader.Pod(version) || version < 1 || version > ProbeVolumeData::CurrentVersion)
    {
      TK_ERR("Probe volume '%s' has version %u, this build reads versions 1 to %u.",
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

    const size_t probeCount = data.ProbeCount();
    const size_t stride     = (size_t) data.CoefficientCount() * 3;

    data.coefficients.assign(probeCount * stride, 0.0f);
    data.valid.assign(probeCount, 0);

    if (version == 1)
    {
      // Dense: every probe, then one byte of mask per probe. Nothing to fill, the old writer
      // stored the neighbours' copies itself.
      if (!reader.Read(data.coefficients.data(), probeCount * stride * sizeof(float)) ||
          !reader.Read(data.valid.data(), probeCount))
      {
        TK_ERR("Probe volume '%s' ended in its payload.", path.c_str());
        return false;
      }

      return true;
    }

    // 2: the mask a bit per probe, then the rows of the probes that bit marks as traced.
    uint64 tracedProbes = 0;
    if (!reader.Pod(tracedProbes) || tracedProbes > (uint64) probeCount)
    {
      TK_ERR("Probe volume '%s' declares %llu traced probes for a %zu probe grid.",
             path.c_str(),
             (unsigned long long) tracedProbes,
             probeCount);
      return false;
    }

    std::vector<uint8> mask(data.MaskBytes(), 0);
    if (!reader.Read(mask.data(), mask.size()))
    {
      TK_ERR("Probe volume '%s' ended in its mask.", path.c_str());
      return false;
    }

    size_t marked = 0;
    for (size_t probe = 0; probe < probeCount; ++probe)
    {
      if ((mask[MaskByteOf(probe)] & MaskBitOf(probe)) == 0)
      {
        continue;
      }

      if (!reader.Read(&data.coefficients[probe * stride], stride * sizeof(float)))
      {
        TK_ERR("Probe volume '%s' ended in its payload.", path.c_str());
        return false;
      }

      data.valid[probe] = 1;
      ++marked;
    }

    if (marked != (size_t) tracedProbes)
    {
      TK_ERR("Probe volume '%s' marks %zu probes as traced but carries %llu.",
             path.c_str(),
             marked,
             (unsigned long long) tracedProbes);
      return false;
    }

    // The writer left the filled probes out, so put them back from their nearest traced neighbour
    // - the same pass, on the same values, that the bake ran before it wrote the file.
    FillUntracedProbes(data);

    return true;
  }

} // namespace ToolKit
