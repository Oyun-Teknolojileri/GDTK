/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file LightProbeBake.h
 *
 * Public surface of the probe bake module. The editor calls it in process, the ProbeBake tool
 * calls it headless; both get the same code path and, because the sampling seed is derived from
 * the probe index, the same bytes.
 */

#include "BakeScene.h"
#include "ProbeVolume.h"

#include <atomic>
#include <functional>

namespace ToolKit
{
  namespace LightProbeBake
  {

    struct BakeRequest
    {
      /** Read synchronously on the calling thread. The snapshot owns nothing afterwards. */
      Scene* scene = nullptr;

      /** World transform of the volume box. */
      Mat4 volumeTransform = Mat4(1.0f);

      /** The box in volume local space. */
      Vec3 boxMin = Vec3(0.0f);
      Vec3 boxMax = Vec3(0.0f);

      /** Probes along each axis. */
      UVec3 resolution = UVec3(8, 4, 8);

      /**
       * Radius, in cells, around the scene in which probes are traced.
       *
       * A probe farther than this from every surface is filled from its nearest traced neighbour,
       * so the grid still interpolates everywhere and only the tracing is saved - in an open scene
       * that is most of the work, and the written file follows: it stores the traced probes only.
       * Nothing samples a probe in open air directly; a surface sits within a cell diagonal of the
       * probes it reads. Zero traces every probe.
       */
      float traceRadius = 2.0f;

      /** Paths traced per probe. */
      int samples = 512;

      /** Surface bounces a path may take. */
      int bounces = 4;

      /** Mixed into the per probe sample seed. */
      uint seed = 0;

      /**
       * Relaxation passes over the grid after tracing, each moving a probe halfway to the mean of
       * its six neighbours. Zero keeps the raw samples. Two is enough to take the salt and pepper
       * out of a probe volume without rounding off the shadows it carries: a probe is a coarse
       * sample of a smooth function, so the noise between neighbours is what the eye reads as
       * clouds on a wall.
       */
      int denoiseIterations = 2;

      /**
       * L1 by default. The runtime uploads the four coefficients of the cosine band a diffuse
       * surface answers to; a quadratic bake is 2.25x the file for a difference that does not show.
       */
      ProbeVolumeQuality quality = ProbeVolumeQuality::L1;

      /** Polled once per probe. May be null. */
      std::atomic<bool>* cancel = nullptr;

      /** Called from a worker thread, roughly every sixteenth probe. May be null. */
      std::function<void(float)> onProgress = nullptr;
    };

    struct BakeResult
    {
      ProbeVolumeData data;
      String error;
      /** Probes the trace actually sampled. */
      uint validProbes   = 0;
      /** Probes nothing could be copied from: a grid with no traced probe at all. */
      uint invalidProbes = 0;
      /** Probes the bake skipped - inside geometry, or too far from any surface - and filled from a
       *  neighbour. They keep a validity mask of zero, the mask records what was traced, and the
       *  writer leaves their coefficients out of the file. */
      uint filledProbes  = 0;
      double seconds       = 0.0;
    };

    /**
     * Traces every probe of the volume and returns the baked coefficients.
     * @return False with `error` filled when the scene has nothing to bake or the request is bad.
     */
    bool Bake(const BakeRequest& request, BakeResult& result);

    /**
     * Traces a scene that the caller already flattened with BuildBakeScene.
     *
     * The snapshot has to be taken on the thread that owns the scene, because BuildBakeScene reads
     * engine objects; only this half, which touches nothing but the snapshot, may run on a worker.
     * Snapshotting from a worker raced the editor's frame loop and wrote NaN coefficients, which
     * the shader then interpolates across the whole volume.
     */
    bool TraceBakeScene(const BakeScene& snapshot, const BakeRequest& request, BakeResult& result);

  } // namespace LightProbeBake
} // namespace ToolKit
