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
 * The data is consumed by the renderer (`s_probeSH`: one array layer per colour channel plus a
 * coverage layer, so a blocked probe can be dropped instead of read as black) and produced by
 * `Modules/LightProbeBake`. Both sides link this header, which is why the format lives in the
 * engine and not in the module.
 *
 * The grid is dense at runtime and sparse in the file. A probe is either traced or filled from its
 * nearest traced neighbour, so a filled probe carries a copy of that neighbour's coefficients: the
 * file leaves them out and `LoadProbeVolume` puts them back with `FillUntracedProbes`. A bake that
 * traced 780 of 2048 probes used to write 98 KB of coefficients; the traced ones are 37 KB, and
 * the grid that comes back is the same one, bit for bit.
 *
 * A copy only travels where the light can: the bake casts one ray to the neighbour it would copy
 * and, when a wall stands in between, records the probe as blocked and leaves it at zero. Handing a
 * probe inside a wall the sky-lit value from the other side of a face is what made walls glow from
 * the inside out.
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
     * 3 is the only layout this build reads: the traced coefficient rows, a bit per probe saying
     * which probes those rows belong to, and a second mask saying which of the skipped probes the
     * bake could not reach. An older file is refused with a message instead of migrated - a volume
     * is cheap to bake again, and carrying retired readers through every change to the fill was the
     * more expensive of the two.
     */
    static constexpr uint CurrentVersion = 3;

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
     * coefficients, so the grid interpolates, and the mask is the record of which probes were
     * actually sampled.
     *
     * This is also what the file stores of it, and what the editor's point view reads to leave the
     * filled probes out of the drawing.
     */
    std::vector<uint8> valid;

    /**
     * One entry per probe, non zero where the bake found no traced probe this one can see, so it
     * carries zeros instead of a copy.
     *
     * The copy exists for the shader, which interpolates the grid trilinearly and would otherwise
     * pull a surface next to a filled probe towards black. But a copy taken through a wall is
     * worse than a zero: a probe in the four centimetre gap between two wall boxes used to inherit
     * the sky-lit air on the other side of a face, and the wall then glowed from the inside out. So
     * the bake casts one ray to the neighbour it would copy and, when geometry stands between them,
     * records that here and leaves the probe alone.
     *
     * Empty when the volume was not checked - a version 1 or 2 file, or a grid whose bake did not
     * run the test. Every untraced probe is then filled, which is what those bakes meant.
     */
    std::vector<uint8> blocked;

    uint CoefficientCount() const { return (uint) quality; }

    size_t ProbeCount() const { return (size_t) resolution.x * resolution.y * resolution.z; }

    /**
     * Bytes the coefficients and the two masks occupy, dense grid included.
     */
    size_t AllocatedBytes() const
    {
      return coefficients.size() * sizeof(float) + valid.size() + blocked.size();
    }

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

    /** Bytes of one validity mask in the file: one bit per probe. The file carries two. */
    size_t MaskBytes() const { return (ProbeCount() + 7) / 8; }

    /** Bytes the file spends on this grid: the two masks and the traced coefficient rows. */
    size_t StoredBytes() const
    {
      return 2 * MaskBytes() + TracedCount() * (size_t) CoefficientCount() * 3 * sizeof(float);
    }
  };

  /** How a fill went: probes it could place, and why the rest stayed zero. */
  struct TK_API ProbeVolumeFill
  {
    /** Probes that took the coefficients of a traced neighbour. */
    uint filled = 0;
    /** Probes left at zero because the shortest way to a traced probe crosses geometry. */
    uint blocked = 0;
    /** Probes left at zero because the grid holds no traced probe at all. */
    uint unfillable = 0;
  };

  /** Marks a probe that has no traced probe to copy from. */
  constexpr uint NoTracedProbe = (uint) -1;

  /**
   * Nearest traced probe for every probe of the grid, in one breadth first walk out of the traced
   * set, `NoTracedProbe` where geometry splits the grid away from all of them.
   *
   * A traced probe maps to itself. The walk takes six steps out of every probe, so the distance is
   * the grid's own Manhattan distance and the first probe to reach a cell wins - which makes the map
   * deterministic and the same on a bake and on a load of its file.
   *
   * This replaced a per probe search that expanded shell by shell around each filled probe: on a
   * `lightroom` bake at 0.1 m spacing, 193254 filled probes cost 7.5 seconds of the 8.8 the whole
   * bake took, while the path traced part was 1.3. One walk over the grid instead of 386k searches
   * is what makes a fine spaced bake finish in a fraction of a second.
   */
  TK_API void BuildNearestTracedMap(const ProbeVolumeData& data, std::vector<uint>& nearest);

  /**
   * Copies the coefficients of the nearest traced probe into every probe the mask marks as filled.
   *
   * The interpolation across a filled probe must not pull a surface towards a hole, so a copy is
   * the answer - but only when the value can travel. A probe the `blocked` mask marks as walled off
   * from every traced neighbour keeps its zeros: copying the sky-lit air from the far side of a
   * face is what made a wall glow from the inside out.
   *
   * A filled probe keeps its zero in the mask, which is what makes the pass idempotent and lets the
   * loader rebuild a file the writer left sparse. Every probe is an independent copy of a traced
   * one, so the grid this produces does not depend on the order the probes are visited in - a bake
   * and a load of the same file agree exactly.
   *
   * @param data Grid to fill in place. Its coefficients and mask have to be dense and consistent;
   *             a volume that does not match those is left untouched.
   * @param nearest A map already built by BuildNearestTracedMap, so a caller that needs it for its
   *                own reasons does not pay for the walk twice. Null builds one here.
   * @return How many probes were filled and how many were left zero, by reason.
   */
  TK_API ProbeVolumeFill FillUntracedProbes(ProbeVolumeData& data,
                                            const std::vector<uint>* nearest = nullptr);

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
