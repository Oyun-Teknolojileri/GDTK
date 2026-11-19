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
       * Relaxes the grid: every probe moves halfway to the mean of its in range neighbours.
       *
       * Each probe is an independent Monte Carlo estimate, so neighbouring probes of a smooth
       * function disagree by the sampling error, and the shader's trilinear interpolation turns that
       * disagreement into clouds on the wall. Reading from a copy makes the pass symmetric, so it
       * does not drift the signal toward the low index corner.
       */
      void DenoiseSh(const UVec3& resolution, std::vector<Sh9>& sh, int iterations)
      {
        if (iterations <= 0 || sh.empty())
        {
          return;
        }

        constexpr float Relaxation = 0.5f;
        std::vector<Sh9> source(sh.size());

        for (int iteration = 0; iteration < iterations; ++iteration)
        {
          source = sh;

          for (uint z = 0; z < resolution.z; ++z)
          {
            for (uint y = 0; y < resolution.y; ++y)
            {
              for (uint x = 0; x < resolution.x; ++x)
              {
                Sh9 mean;
                for (int c = 0; c < ShCoefficientCount; ++c)
                {
                  mean.c[c] = Vec3(0.0f);
                }
                int neighbours = 0;

                const auto accumulate = [&](uint nz, uint ny, uint nx) -> void
                {
                  const Sh9& other = source[((size_t) nz * resolution.y + ny) * resolution.x + nx];
                  for (int c = 0; c < ShCoefficientCount; ++c)
                  {
                    mean.c[c] += other.c[c];
                  }
                  ++neighbours;
                };

                if (x > 0) accumulate(z, y, x - 1);
                if (x + 1 < resolution.x) accumulate(z, y, x + 1);
                if (y > 0) accumulate(z, y - 1, x);
                if (y + 1 < resolution.y) accumulate(z, y + 1, x);
                if (z > 0) accumulate(z - 1, y, x);
                if (z + 1 < resolution.z) accumulate(z + 1, y, x);

                if (neighbours == 0)
                {
                  continue;
                }

                Sh9& target = sh[((size_t) z * resolution.y + y) * resolution.x + x];
                const float inv = 1.0f / (float) neighbours;
                for (int c = 0; c < ShCoefficientCount; ++c)
                {
                  const Vec3 averaged = mean.c[c] * inv;
                  target.c[c]         = glm::mix(target.c[c], averaged, Relaxation);
                }
              }
            }
          }
        }
      }

      /** Deterministic per probe stream, independent of how the pool split the work. */
      Pcg32 ProbeRng(size_t probeIndex, uint seed)
      {
        const uint64 mixed = (uint64) probeIndex * 0x9e3779b97f4a7c15ULL ^ ((uint64) seed << 32u);
        return Pcg32(mixed);
      }

      /**
       * Copies the coefficients of the nearest valid probe into `target`.
       * @return False when no probe of the grid was valid.
       */
      bool FillFromNearestValid(const UVec3& resolution,
                                const std::vector<uint8>& valid,
                                std::vector<Sh9>& sh,
                                uint targetX,
                                uint targetY,
                                uint targetZ)
      {
        const int maxRadius = (int) glm::max(glm::max(resolution.x, resolution.y), resolution.z);

        for (int radius = 1; radius <= maxRadius; ++radius)
        {
          for (int dz = -radius; dz <= radius; ++dz)
          {
            for (int dy = -radius; dy <= radius; ++dy)
            {
              for (int dx = -radius; dx <= radius; ++dx)
              {
                // Only the shell at this radius, the inside was searched already.
                if (glm::abs(dx) != radius && glm::abs(dy) != radius && glm::abs(dz) != radius)
                {
                  continue;
                }

                const int x = (int) targetX + dx;
                const int y = (int) targetY + dy;
                const int z = (int) targetZ + dz;
                if (x < 0 || y < 0 || z < 0 || x >= (int) resolution.x || y >= (int) resolution.y ||
                    z >= (int) resolution.z)
                {
                  continue;
                }

                const size_t index = ((size_t) z * resolution.y + y) * resolution.x + x;
                if (!valid[index])
                {
                  continue;
                }

                sh[((size_t) targetZ * resolution.y + targetY) * resolution.x + targetX] = sh[index];
                return true;
              }
            }
          }
        }

        return false;
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
        TK_LOG("Probe bake: the scene has no gradient sky the baker samples, so paths that miss "
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

      const int samples = glm::max(1, request.samples);
      const TraceContext traceContext {&scene, glm::max(1, request.bounces)};

      std::vector<Sh9> sh(probeCount);
      std::vector<uint8> valid(probeCount, 1);
      std::atomic<size_t> completed {0};

      {
        using poolstl::iota_iter;

        std::for_each(TKExecByConditional(true, WorkerManager::BulkPool),
                      iota_iter<size_t>(0),
                      iota_iter<size_t>(probeCount),
                      [&](size_t index)
                      {
                        sh[index] = Sh9 {};

                        if (request.cancel != nullptr && request.cancel->load())
                        {
                          return;
                        }

                        if (IsProbeEnclosed(scene, positions[index], enclosureDistance))
                        {
                          valid[index] = 0;
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

                            for (int coefficient = 0; coefficient < ShCoefficientCount; ++coefficient)
                            {
                              sh[index].c[coefficient] += radiance * basis[coefficient];
                            }
                          }

                          // 4 pi / N for the sphere integral, then the cosine convolution and
                          // the division by pi that the shader expects.
                          const float scale = 4.0f * Pi / (float) samples;
                          for (int coefficient = 0; coefficient < ShCoefficientCount; ++coefficient)
                          {
                            sh[index].c[coefficient] *= scale * ShBandFactor[coefficient];
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

      // Fill the probes that sit inside geometry from the nearest valid neighbour, so the
      // interpolation across a wall does not pull towards black.
      uint buriedProbes = 0;
      for (uint z = 0; z < resolution.z; ++z)
      {
        for (uint y = 0; y < resolution.y; ++y)
        {
          for (uint x = 0; x < resolution.x; ++x)
          {
            const size_t index = ((size_t) z * resolution.y + y) * resolution.x + x;
            if (valid[index])
            {
              continue;
            }

            buriedProbes++;
            if (FillFromNearestValid(resolution, valid, sh, x, y, z))
            {
              // The filled probe keeps a valid mask of one: its neighbours are its data.
              valid[index] = 1;
            }
            else
            {
              result.invalidProbes++;
            }
          }
        }
      }
      result.buriedProbes = buriedProbes;

      DenoiseSh(resolution, sh, request.denoiseIterations);
      if (request.denoiseIterations > 0)
      {
        TK_LOG("Probe bake: denoised the grid with %d passes.", request.denoiseIterations);
      }

      const uint coefficientCount = (uint) request.quality;

      ProbeVolumeData& data = result.data;
      data.version          = ProbeVolumeData::CurrentVersion;
      data.resolution       = resolution;
      data.transform        = request.volumeTransform;
      data.boxMin           = request.boxMin;
      data.boxMax           = request.boxMax;
      data.quality          = request.quality;
      data.coefficients.resize(probeCount * coefficientCount * 3);
      data.valid.resize(probeCount);

      for (size_t probe = 0; probe < probeCount; ++probe)
      {
        for (uint coefficient = 0; coefficient < coefficientCount; ++coefficient)
        {
          const Vec3& value = sh[probe].c[coefficient];
          const size_t base = (probe * coefficientCount + coefficient) * 3;

          data.coefficients[base + 0] = value.x;
          data.coefficients[base + 1] = value.y;
          data.coefficients[base + 2] = value.z;
        }

        data.valid[probe] = valid[probe];
        if (valid[probe])
        {
          result.validProbes++;
        }
      }

      const auto endTime = std::chrono::steady_clock::now();
      result.seconds =
          std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime).count();

      TK_LOG("Probe bake: %u valid, %u buried (filled from a neighbour), %u unfillable, %.2f seconds, %.1f KB.",
             result.validProbes,
             result.buriedProbes,
             result.invalidProbes,
             result.seconds,
             (double) data.AllocatedBytes() / 1024.0);

      return true;
    }

  } // namespace LightProbeBake
} // namespace ToolKit
