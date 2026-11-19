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

      /** Paths traced per probe. */
      int samples = 512;

      /** Surface bounces a path may take. */
      int bounces = 4;

      /** Mixed into the per probe sample seed. */
      uint seed = 0;

      ProbeVolumeQuality quality = ProbeVolumeQuality::L2;

      /** Polled once per probe. May be null. */
      std::atomic<bool>* cancel = nullptr;

      /** Called from a worker thread, roughly every sixteenth probe. May be null. */
      std::function<void(float)> onProgress = nullptr;
    };

    struct BakeResult
    {
      ProbeVolumeData data;
      String error;
      uint validProbes   = 0;
      uint invalidProbes = 0;
      uint buriedProbes  = 0;
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
