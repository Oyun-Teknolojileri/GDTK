/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "LightProbeBake.h"

#include "PathTracer.h"
#include "Sh.h"

#include "Scene.h"
#include "Threads.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <numeric>

namespace ToolKit
{
  namespace LightProbeBake
  {
    namespace
    {
      constexpr float Pi = 3.14159265359f;

      /** Fraction of the smallest cell a probe has to be to count as buried in geometry. */
      constexpr float EnclosureFraction = 0.25f;

      /** How often the progress callback fires, in probes. */
      constexpr size_t ProgressStride = 16;

      /**
       * Shortens the fill's line of sight test, so the surface the neighbour sits on does not count
       * as blocking the value that neighbour already carries.
       */
      constexpr float FillRayShrink = 1.0e-4f;

      /** A probe is inside geometry when a surface is that close on both sides of one axis. */
      bool IsProbeEnclosed(const BakeScene& scene, const Vec3& position, float enclosureDistance)
      {
        for (int axis = 0; axis < 3; ++axis)
        {
          Vec3 direction(0.0f);
          direction[axis] = 1.0f;

          const Vec3 origin = position + direction * 1e-4f;
          const bool positive =
              scene.bvh.Occluded(origin, direction, enclosureDistance + 1e-4f, &scene.materialsPassingLight);

          const Vec3 negativeOrigin = position - direction * 1e-4f;
          const bool negative = scene.bvh.Occluded(negativeOrigin,
                                                   -direction,
                                                   enclosureDistance + 1e-4f,
                                                   &scene.materialsPassingLight);

          if (positive && negative)
          {
            return true;
          }
        }

        return false;
      }

      /**
       * True when no surface is within `distance` along any of the six axes.
       *
       * A probe in open air only serves the interpolation of its neighbours: nothing samples it
       * directly, and the irradiance field out there is smooth, so tracing it spends the expensive
       * part of the bake - samples times bounces ray casts with a shadow ray per hit - on a value a
       * neighbour can stand in for. Six axis rays are a cheap lower bound on the distance, and the
       * multiplier in the caller is what keeps the bound on the safe side.
       */
      bool IsProbeFarFromGeometry(const BakeScene& scene, const Vec3& position, float distance)
      {
        for (int axis = 0; axis < 3; ++axis)
        {
          Vec3 direction(0.0f);
          direction[axis] = 1.0f;

          if (scene.bvh.Occluded(position, direction, distance) ||
              scene.bvh.Occluded(position, -direction, distance))
          {
            return false;
          }
        }

        return true;
      }

      /**
       * Marks the probes whose shortest way to a traced neighbour crosses geometry.
       *
       * The fill exists so the shader's trilinear interpolation cannot pull a surface next to a
       * skipped probe towards a hole, and a copy is the right answer as long as the value could
       * have reached the probe. It could not here: a probe in the four centimetre gap between two
       * wall boxes used to be handed the sky-lit air on the far side of a face, and the wall then
       * glowed from the inside out. One ray to the very neighbour the fill would copy decides it,
       * so the two agree on which neighbour "the" candidate is - the bake only records whether the
       * value can travel that far.
       */
      void MarkBlockedFills(const BakeScene& scene,
                            const std::vector<Vec3>& positions,
                            const std::vector<uint>& nearest,
                            ProbeVolumeData& data)
      {
        const size_t probeCount = data.ProbeCount();
        data.blocked.assign(probeCount, 0);

        for (size_t probe = 0; probe < probeCount; ++probe)
        {
          if (data.valid[probe] || probe >= positions.size() || nearest.size() != probeCount)
          {
            continue;
          }

          const uint source = nearest[probe];
          if (source == NoTracedProbe)
          {
            // No traced probe anywhere: the fill reports that as unfillable, not as walled off.
            continue;
          }

          const Vec3 from      = positions[probe];
          const Vec3 delta     = positions[(size_t) source] - from;
          const float distance = glm::length(delta);
          if (distance <= 0.0f)
          {
            continue;
          }

          RayHit hit;
          if (scene.bvh.Intersect(from, delta / distance, distance - FillRayShrink, hit))
          {
            data.blocked[probe] = 1;
          }
        }
      }

      /**
       * Relaxes the grid: every probe moves halfway to the mean of its in range neighbours.
       *
       * Each probe is an independent Monte Carlo estimate, so neighbouring probes of a smooth
       * function disagree by the sampling error, and the shader's trilinear interpolation turns that
       * disagreement into clouds on the wall. Reading from a copy makes the pass symmetric, so it
       * does not drift the signal toward the low index corner.
       *
       * It relaxes the stored coefficients only, and each of them independently, so running it on
       * the grid the file keeps gives the same numbers the quadratic set gave for its first four.
       *
       * Only the probes the bake traced take part, on both sides of the average. A probe it skipped
       * holds no measurement of its own - it is filled from a neighbour afterwards - and letting it
       * into the mean let the light around the outside of a wall spread into the inside: a closed
       * box whose interior had traced to black came back grey after eight passes.
       *
       * A neighbour also has to be visible. The grid knows nothing about geometry and a cell can
       * straddle a surface - any face with no thickness, or a wall thinner than a cell, which is
       * most walls at a twenty five centimetre spacing - so an average across one carried the
       * brightness of the lit side into the dark side. `visible` is the six bit per probe record of
       * which neighbours a ray reaches, cast once for the whole bake and reused by every iteration.
       */
      void DenoiseProbes(ProbeVolumeData& data, int iterations, const std::vector<uint8>& visible)
      {
        const UVec3 resolution = data.resolution;
        const size_t stride     = (size_t) data.CoefficientCount() * 3;
        const size_t probeCount = data.ProbeCount();

        if (iterations <= 0 || probeCount == 0 || data.coefficients.size() != probeCount * stride ||
            data.valid.size() != probeCount)
        {
          return;
        }

        // Without a visibility record the pass falls back to the geometry blind behaviour, which is
        // what a caller that has no scene to trace can ask for.
        const bool sighted = visible.size() == probeCount;

        constexpr float Relaxation = 0.5f;
        std::vector<float> source(data.coefficients.size());
        std::vector<float> mean(stride, 0.0f);
        float* coefficients = data.coefficients.data();

        for (int iteration = 0; iteration < iterations; ++iteration)
        {
          source = data.coefficients;

          for (uint z = 0; z < resolution.z; ++z)
          {
            for (uint y = 0; y < resolution.y; ++y)
            {
              for (uint x = 0; x < resolution.x; ++x)
              {
                const size_t self = ((size_t) z * resolution.y + y) * resolution.x + x;
                if (!data.valid[self])
                {
                  continue;
                }

                std::fill(mean.begin(), mean.end(), 0.0f);
                int neighbours = 0;

                const auto accumulate = [&](uint nz, uint ny, uint nx, int face) -> void
                {
                  const size_t index = ((size_t) nz * resolution.y + ny) * resolution.x + nx;
                  if (!data.valid[index])
                  {
                    return;
                  }

                  if (sighted && (visible[self] & (uint8) (1u << face)) == 0)
                  {
                    return;
                  }

                  const float* other = &source[index * stride];
                  for (size_t entry = 0; entry < stride; ++entry)
                  {
                    mean[entry] += other[entry];
                  }
                  ++neighbours;
                };

                if (x > 0) accumulate(z, y, x - 1, 0);
                if (x + 1 < resolution.x) accumulate(z, y, x + 1, 1);
                if (y > 0) accumulate(z, y - 1, x, 2);
                if (y + 1 < resolution.y) accumulate(z, y + 1, x, 3);
                if (z > 0) accumulate(z - 1, y, x, 4);
                if (z + 1 < resolution.z) accumulate(z + 1, y, x, 5);

                if (neighbours == 0)
                {
                  continue;
                }

                float* target      = &coefficients[self * stride];
                const float inv    = 1.0f / (float) neighbours;
                for (size_t entry = 0; entry < stride; ++entry)
                {
                  target[entry] = glm::mix(target[entry], mean[entry] * inv, Relaxation);
                }
              }
            }
          }
        }
      }

      /**
       * Six bits per probe: which of its face neighbours a ray reaches.
       *
       * The bits are the -x, +x, -y, +y, -z, +z neighbours, in the order the denoise asks for them.
       * One ray per probe per direction, cast once for a whole bake, is what keeps a blur from
       * crossing the geometry a cell straddles.
       */
      std::vector<uint8> BuildNeighbourVisibility(const BakeScene& scene,
                                                  const std::vector<Vec3>& positions,
                                                  const UVec3& resolution)
      {
        constexpr float NeighbourShrink = 1.0e-3f;
        constexpr int Step[6][3]        = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                                           {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};

        std::vector<uint8> visible(positions.size(), 0);

        for (uint z = 0; z < resolution.z; ++z)
        {
          for (uint y = 0; y < resolution.y; ++y)
          {
            for (uint x = 0; x < resolution.x; ++x)
            {
              const size_t index = ((size_t) z * resolution.y + y) * resolution.x + x;
              const Vec3 from    = positions[index];

              for (int face = 0; face < 6; ++face)
              {
                const int nx = (int) x + Step[face][0];
                const int ny = (int) y + Step[face][1];
                const int nz = (int) z + Step[face][2];
                if (nx < 0 || ny < 0 || nz < 0 || nx >= (int) resolution.x ||
                    ny >= (int) resolution.y || nz >= (int) resolution.z)
                {
                  continue;
                }

                const size_t other = ((size_t) nz * resolution.y + ny) * resolution.x + nx;
                const Vec3 delta   = positions[other] - from;
                const float distance = glm::length(delta);
                if (distance <= NeighbourShrink)
                {
                  continue;
                }

                RayHit hit;
                if (!scene.bvh.Intersect(from, delta / distance, distance - NeighbourShrink, hit))
                {
                  visible[index] |= (uint8) (1u << face);
                }
              }
            }
          }
        }

        return visible;
      }

      /** Deterministic per probe stream, independent of how the pool split the work. */
      Pcg32 ProbeRng(size_t probeIndex, uint seed)
      {
        const uint64 mixed = (uint64) probeIndex * 0x9e3779b97f4a7c15ULL ^ ((uint64) seed << 32u);
        return Pcg32(mixed);
      }
    } // namespace

    bool Bake(const BakeRequest& request, BakeResult& result)
    {
      result = BakeResult {};

      if (request.scene == nullptr)
      {
        result.error = "No scene to bake.";
        return false;
      }

      const auto snapshotStart = std::chrono::steady_clock::now();

      BakeScene scene;
      if (!BuildBakeScene(request.scene, scene, result.error))
      {
        return false;
      }

      const double snapshotSeconds =
          std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() -
                                                                    snapshotStart)
              .count();

      // The snapshot reads engine objects, so it happens on the calling thread. A caller that wants
      // the tracing off the frame thread snapshots first and calls TraceBakeScene itself, which is
      // what the editor does: snapshotting from a worker raced the frame loop and wrote NaN
      // coefficients into the file.
      if (!TraceBakeScene(scene, request, result))
      {
        return false;
      }

      result.seconds += snapshotSeconds;
      return true;
    }

    bool TraceBakeScene(const BakeScene& bakeScene, const BakeRequest& request, BakeResult& result)
    {
      const auto startTime = std::chrono::steady_clock::now();

      result = BakeResult {};

      if (bakeScene.Empty())
      {
        result.error = "No geometry to trace.";
        return false;
      }

      const BakeScene& scene = bakeScene;

      const UVec3 resolution = glm::max(request.resolution, UVec3(1));
      const size_t probeCount = (size_t) resolution.x * resolution.y * resolution.z;

      if (probeCount == 0)
      {
        result.error = "The volume has an empty resolution.";
        return false;
      }

      const Vec3 boxSize = request.boxMax - request.boxMin;
      if (glm::any(glm::lessThanEqual(boxSize, Vec3(0.0f))))
      {
        result.error = "The volume box is empty.";
        return false;
      }

      TK_LOG("Probe bake: %zu probes (%ux%ux%u), %zu triangles, %zu lights, %d samples, %d bounces.",
             probeCount,
             resolution.x,
             resolution.y,
             resolution.z,
             scene.bvh.Triangles().size(),
             scene.lights.size(),
             request.samples,
             request.bounces);

      if (!scene.sky.enabled)
      {
        TK_LOG("Probe bake: the scene has no sky the baker can sample, so paths that miss "
               "contribute nothing.");
      }

      // World positions of the cell centres.
      std::vector<Vec3> positions(probeCount);
      for (uint z = 0; z < resolution.z; ++z)
      {
        for (uint y = 0; y < resolution.y; ++y)
        {
          for (uint x = 0; x < resolution.x; ++x)
          {
            const Vec3 uvw((x + 0.5f) / (float) resolution.x,
                           (y + 0.5f) / (float) resolution.y,
                           (z + 0.5f) / (float) resolution.z);
            const Vec3 local = request.boxMin + uvw * boxSize;

            const size_t index = ((size_t) z * resolution.y + y) * resolution.x + x;
            positions[index]   = Vec3(request.volumeTransform * Vec4(local, 1.0f));
          }
        }
      }

      const float smallestCell = glm::min(glm::min(boxSize.x / (float) resolution.x,
                                                   boxSize.y / (float) resolution.y),
                                          boxSize.z / (float) resolution.z);
      const float enclosureDistance = smallestCell * EnclosureFraction;

      // The axis ray test below reports a distance about 1.4 times the true one for a surface seen
      // at an angle, so the radius is stretched to keep the probes a surface interpolates from
      // inside the traced set.
      constexpr float SparseAxisSafety = 1.5f;
      const float traceDistance = smallestCell * glm::max(request.traceRadius, 0.0f) * SparseAxisSafety;

      if (request.traceRadius > 0.0f)
      {
        TK_LOG("Probe bake: tracing the probes within %.2f world units of a surface, filling the "
               "rest from a neighbour.",
               traceDistance);
      }

      const int samples = glm::max(1, request.samples);
      const TraceContext traceContext {&scene, glm::max(1, request.bounces)};

      const uint coefficientCount = (uint) request.quality;
      const size_t stride         = (size_t) coefficientCount * 3;

      // The trace writes the grid the file and the load are built on, not an intermediate set the
      // rest of the bake converts from: a quadratic projection is trimmed to the coefficients the
      // quality stores, and each of them is independent of the others, so the stored values are
      // the ones the full set would have given.
      ProbeVolumeData& data = result.data;
      data.version          = ProbeVolumeData::CurrentVersion;
      data.resolution       = resolution;
      data.transform        = request.volumeTransform;
      data.boxMin           = request.boxMin;
      data.boxMax           = request.boxMax;
      data.quality          = request.quality;
      data.coefficients.assign(probeCount * stride, 0.0f);
      data.valid.assign(probeCount, 1);

      std::atomic<size_t> completed {0};

      {
        using poolstl::iota_iter;

        std::for_each(TKExecByConditional(true, WorkerManager::BulkPool),
                      iota_iter<size_t>(0),
                      iota_iter<size_t>(probeCount),
                      [&](size_t index)
                      {
                        float* row = &data.coefficients[index * stride];

                        if (request.cancel != nullptr && request.cancel->load())
                        {
                          return;
                        }

                        if (IsProbeEnclosed(scene, positions[index], enclosureDistance) ||
                            (traceDistance > 0.0f &&
                             IsProbeFarFromGeometry(scene, positions[index], traceDistance)))
                        {
                          data.valid[index] = 0;
                        }
                        else
                        {
                          Pcg32 rng = ProbeRng(index, request.seed);

                          for (int sample = 0; sample < samples; ++sample)
                          {
                            const Vec3 direction = rng.SampleUniformSphere();
                            const Vec3 radiance  = TraceRadiance(traceContext, positions[index], direction, rng);

                            float basis[ShCoefficientCount];
                            ShBasis(direction, basis);

                            for (uint coefficient = 0; coefficient < coefficientCount; ++coefficient)
                            {
                              const Vec3 weighted = radiance * basis[coefficient];
                              row[coefficient * 3 + 0] += weighted.x;
                              row[coefficient * 3 + 1] += weighted.y;
                              row[coefficient * 3 + 2] += weighted.z;
                            }
                          }

                          // 4 pi / N for the sphere integral, then the cosine convolution and
                          // the division by pi that the shader expects.
                          const float scale = 4.0f * Pi / (float) samples;
                          for (uint coefficient = 0; coefficient < coefficientCount; ++coefficient)
                          {
                            const float factor = scale * ShBandFactor[coefficient];
                            row[coefficient * 3 + 0] *= factor;
                            row[coefficient * 3 + 1] *= factor;
                            row[coefficient * 3 + 2] *= factor;
                          }
                        }

                        const size_t done = completed.fetch_add(1) + 1;
                        if (request.onProgress != nullptr &&
                            (done % ProgressStride == 0 || done == probeCount))
                        {
                          request.onProgress((float) done / (float) probeCount);
                        }
                      });
      }

      if (request.cancel != nullptr && request.cancel->load())
      {
        result.error = "The bake was cancelled.";
        return false;
      }

      // The relaxation reads the traced probes only, so it runs on its own: nothing has to be
      // filled before it, and the single fill below is the one the writer leaves out of the file
      // and the loader repeats. Its neighbour visibility is not needed when it will not run.
      std::vector<uint8> neighbourVisible;
      if (request.denoiseIterations > 0)
      {
        neighbourVisible = BuildNeighbourVisibility(scene, positions, resolution);
      }

      DenoiseProbes(data, request.denoiseIterations, neighbourVisible);
      if (request.denoiseIterations > 0)
      {
        TK_LOG("Probe bake: denoised the grid with %d passes over the traced probes.",
               request.denoiseIterations);
      }

      // One walk over the grid answers "which traced probe is nearest" for every probe, and both
      // the blocked marking and the fill read it. Building it per probe was the whole cost of a
      // fine spaced bake.
      std::vector<uint> nearest;
      BuildNearestTracedMap(data, nearest);

      MarkBlockedFills(scene, positions, nearest, data);

      const ProbeVolumeFill fill = FillUntracedProbes(data, &nearest);

      result.validProbes   = (uint) data.TracedCount();
      result.filledProbes  = fill.filled;
      result.invalidProbes = fill.blocked + fill.unfillable;

      const auto endTime = std::chrono::steady_clock::now();
      result.seconds =
          std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime).count();

      TK_LOG("Probe bake: %u traced, %u filled from a neighbour, %u left zero (%u of them walled "
             "off from every traced neighbour, %u with nothing to copy from), %.2f seconds, "
             "%.1f KB dense, %.1f KB stored.",
             result.validProbes,
             result.filledProbes,
             result.invalidProbes,
             fill.blocked,
             fill.unfillable,
             result.seconds,
             (double) data.AllocatedBytes() / 1024.0,
             (double) data.StoredBytes() / 1024.0);

      return true;
    }

  } // namespace LightProbeBake
} // namespace ToolKit
