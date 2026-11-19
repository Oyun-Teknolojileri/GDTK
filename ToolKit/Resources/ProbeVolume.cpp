/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "ProbeVolume.h"

#include "Logger.h"

#include <cstring>
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

    template <typename T>
    bool ReadPod(std::ifstream& stream, T& value)
    {
      stream.read(reinterpret_cast<char*>(&value), sizeof(T));
      return stream.good();
    }
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
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
      TK_ERR("Probe volume '%s' does not exist.", path.c_str());
      return false;
    }

    char magic[sizeof(ProbeVolumeMagic)] = {0};
    stream.read(magic, sizeof(magic));
    if (!stream.good() || std::memcmp(magic, ProbeVolumeMagic, sizeof(magic)) != 0)
    {
      TK_ERR("Probe volume '%s' is not a .pgi file.", path.c_str());
      return false;
    }

    uint version = 0;
    if (!ReadPod(stream, version) || version != ProbeVolumeData::CurrentVersion)
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
    if (!ReadPod(stream, resX) || !ReadPod(stream, resY) || !ReadPod(stream, resZ) ||
        !ReadPod(stream, quality))
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

    stream.read(reinterpret_cast<char*>(glm::value_ptr(data.transform)), sizeof(float) * 16);
    stream.read(reinterpret_cast<char*>(glm::value_ptr(data.boxMin)), sizeof(float) * 3);
    stream.read(reinterpret_cast<char*>(glm::value_ptr(data.boxMax)), sizeof(float) * 3);
    if (!stream.good())
    {
      TK_ERR("Probe volume '%s' ended in its transform.", path.c_str());
      return false;
    }

    uint64 storedProbes = 0;
    if (!ReadPod(stream, storedProbes) || storedProbes != (uint64) data.ProbeCount())
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
    stream.read(reinterpret_cast<char*>(data.coefficients.data()),
                (std::streamsize) (coefficientCount * sizeof(float)));

    data.valid.resize(data.ProbeCount());
    stream.read(reinterpret_cast<char*>(data.valid.data()), (std::streamsize) data.ProbeCount());

    if (!stream.good())
    {
      TK_ERR("Probe volume '%s' ended in its payload.", path.c_str());
      return false;
    }

    return true;
  }

} // namespace ToolKit
