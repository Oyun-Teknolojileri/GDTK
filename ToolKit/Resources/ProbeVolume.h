/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file ProbeVolume.h
 *
 * Baked diffuse irradiance for a grid of light probes, and the file format it travels in.
 *
 * The data is consumed by the renderer (`s_probeSH`, a three layer coefficient array) and produced
 * by `Modules/LightProbeBake`. Both sides link this header, which is why the format lives in the
 * engine and not in the module.
 */

#include "Types.h"

namespace ToolKit
{

  /**
   * Number of spherical harmonic coefficients stored per probe.
   *
   * The value is also the number of coefficients the baker projects, so it is the enum's
   * payload and not a separate flag.
   */
  enum class ProbeVolumeQuality : uint8
  {
    L1 = 4, //!< Y00, Y1-1, Y10, Y11. One RGBA16F texture at runtime.
    L2 = 9  //!< The full quadratic set. Three RGBA16F textures at runtime.
  };

  /**
   * A dense grid of probes over an oriented box, each holding the irradiance arriving at its
   * cell centre, divided by pi and expanded in spherical harmonics.
   *
   * Coefficients are the same quantity the sky irradiance cubemap holds, so the shader can
   * multiply the albedo with either without a fudge factor.
   */
  struct TK_API ProbeVolumeData
  {
    /** Bump when the binary layout below changes. */
    static constexpr uint CurrentVersion = 1;

    uint version = CurrentVersion;

    /** Probes along x, y and z. Each axis has to be at least one. */
    UVec3 resolution = UVec3(1);

    /** World transform of the volume box. Rotation and scale are allowed. */
    Mat4 transform = Mat4(1.0f);

    /** The box in the volume's local space. */
    Vec3 boxMin = Vec3(0.0f);
    Vec3 boxMax = Vec3(0.0f);

    ProbeVolumeQuality quality = ProbeVolumeQuality::L2;

    /**
     * Probe major, then coefficient major, then rgb. `probeCount * coefficientCount * 3`
     * floats. Coefficients are already multiplied by the cosine convolution for their band
     * and divided by pi.
     */
    std::vector<float> coefficients;

    /** One entry per probe. False for a probe the baker found inside geometry. */
    std::vector<uint8> valid;

    uint CoefficientCount() const { return (uint) quality; }

    size_t ProbeCount() const { return (size_t) resolution.x * resolution.y * resolution.z; }

    /** Bytes the coefficients and the validity mask occupy. */
    size_t AllocatedBytes() const { return coefficients.size() * sizeof(float) + valid.size(); }
  };

  /**
   * Writes the volume to `path` in the `.pgi` binary format.
   * @return False when the file can not be opened or the data is inconsistent.
   */
  TK_API bool SaveProbeVolume(const String& path, const ProbeVolumeData& data);

  /**
   * Reads a volume written by SaveProbeVolume.
   * @return False on a missing file, a bad magic or a version this build does not know.
   */
  TK_API bool LoadProbeVolume(const String& path, ProbeVolumeData& data);

} // namespace ToolKit
