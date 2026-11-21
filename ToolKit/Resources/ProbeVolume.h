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
 *
 * The grid is dense at runtime and sparse in the file. A probe is either traced or filled from its
 * nearest traced neighbour, so a filled probe carries a copy of that neighbour's coefficients: the
 * file leaves them out and `LoadProbeVolume` puts them back with `FillUntracedProbes`. A bake that
 * traced 780 of 2048 probes used to write 98 KB of coefficients; the traced ones are 37 KB, and
 * the grid that comes back is the same one, bit for bit.
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
    /**
     * Bump when the binary layout below changes.
     *
     * 1 stores every probe and one byte of mask per probe. 2 stores the traced probes only and
     * packs the mask a bit per probe; both load to the same dense grid, so a 1 file is still read.
     */
    static constexpr uint CurrentVersion = 2;

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
     *
     * The renderer samples this densely, so a probe the bake did not trace carries the
     * coefficients of its nearest traced neighbour and the array is always full. The file stores
     * the traced rows only; see `valid`.
     */
    std::vector<float> coefficients;

    /**
     * One entry per probe. False for a probe the baker did not trace: it is inside geometry, or it
     * is farther than the bake's sparse radius from every surface. Such a probe still carries
     * coefficients - they are copied from its nearest traced neighbour, so the grid interpolates -
     * and the mask is the record of which probes were actually sampled.
     *
     * This is also what the file stores of it, and what the editor's point view reads to leave the
     * filled probes out of the drawing.
     */
    std::vector<uint8> valid;

    uint CoefficientCount() const { return (uint) quality; }

    size_t ProbeCount() const { return (size_t) resolution.x * resolution.y * resolution.z; }

    /** Bytes the coefficients and the validity mask occupy, dense grid included. */
    size_t AllocatedBytes() const { return coefficients.size() * sizeof(float) + valid.size(); }

    /**
     * Probes the mask marks as traced.
     *
     * A volume that carries no mask marks every probe traced, which is what a caller that never
     * ran a sparse bake means and what the writer does with the mask it is given.
     */
    size_t TracedCount() const
    {
      if (valid.size() != ProbeCount())
      {
        return ProbeCount();
      }

      size_t traced = 0;
      for (uint8 entry : valid)
      {
        if (entry)
        {
          ++traced;
        }
      }

      return traced;
    }

    /** Bytes of the validity mask in the file: one bit per probe. */
    size_t MaskBytes() const { return (ProbeCount() + 7) / 8; }

    /** Bytes the file spends on this grid: the mask and the traced coefficient rows. */
    size_t StoredBytes() const
    {
      return MaskBytes() + TracedCount() * (size_t) CoefficientCount() * 3 * sizeof(float);
    }
  };

  /** How a fill went: probes it could place, and probes with no traced neighbour anywhere. */
  struct TK_API ProbeVolumeFill
  {
    uint filled     = 0;
    uint unfillable = 0;
  };

  /**
   * Copies the coefficients of the nearest traced probe into every probe the mask marks as filled.
   *
   * The search walks outward shell by shell around the target, so the copy comes from the closest
   * traced probe and the interpolation across it cannot pull towards a hole. A filled probe keeps
   * its zero in the mask, which is what makes the pass idempotent and lets the loader rebuild a
   * file the writer left sparse.
   *
   * Every probe is an independent copy of a traced one, so the grid this produces does not depend
   * on the order the probes are visited in - a bake and a load of the same file agree exactly.
   *
   * @param data Grid to fill in place. Its coefficients and mask have to be dense and consistent;
   *             a volume that does not match those are left untouched.
   * @return How many probes were filled and how many had no traced probe to copy from.
   */
  TK_API ProbeVolumeFill FillUntracedProbes(ProbeVolumeData& data);

  /**
   * Writes the volume to `path` in the `.pgi` binary format.
   *
   * The coefficients of the probes the mask marks as filled are not written: they are a copy of a
   * neighbour, and `LoadProbeVolume` puts them back.
   *
   * @return False when the file can not be opened or the data is inconsistent.
   */
  TK_API bool SaveProbeVolume(const String& path, const ProbeVolumeData& data);

  /**
   * Reads a volume written by SaveProbeVolume and fills the probes the file left out.
   * @return False on a missing file, a bad magic or a version this build does not know.
   */
  TK_API bool LoadProbeVolume(const String& path, ProbeVolumeData& data);

} // namespace ToolKit
