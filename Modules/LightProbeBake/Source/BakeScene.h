/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file BakeScene.h
 *
 * Snapshot of a scene in the form the ray tracer wants: world space triangles, the few
 * material fields that matter, the lights and the sky.
 *
 * The snapshot is taken synchronously on the calling thread and owns no engine object, so the
 * scene may be edited while the bake runs.
 */

#include "BakeTypes.h"
#include "Bvh.h"

#include <vector>

namespace ToolKit
{
  class Scene;

  namespace LightProbeBake
  {

    struct BakeScene
    {
      std::vector<SurfaceMaterial> materials;
      std::vector<SceneLight> lights;
      SkyModel sky;
      Bvh bvh;

      /** Bounds of the static geometry, used to fit a volume when the caller did not. */
      Vec3 boundsMin = Vec3(0.0f);
      Vec3 boundsMax = Vec3(0.0f);

      bool Empty() const { return bvh.Empty(); }
    };

    /**
     * Flattens the visible static meshes of `scene` into `out`.
     * @return False and fills `error` when the scene has nothing to trace.
     */
    bool BuildBakeScene(Scene* scene, BakeScene& out, String& error);

  } // namespace LightProbeBake
} // namespace ToolKit
