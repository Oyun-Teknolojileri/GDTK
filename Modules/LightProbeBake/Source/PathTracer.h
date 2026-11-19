/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file PathTracer.h
 *
 * Unidirectional path tracer for the probe bake. Diffuse only: the transport is Lambert
 * reflection, emissive surfaces and the sky, which is all a spherical harmonic projection of
 * the irradiance can carry anyway.
 */

#include "BakeScene.h"

namespace ToolKit
{
  namespace LightProbeBake
  {
    struct TraceContext
    {
      const BakeScene* scene = nullptr;

      /** How many surface bounces a path may take before it is terminated. */
      int maxBounces = 4;
    };

    /** Radiance arriving at `origin` from `direction`, in the engine's linear HDR units. */
    Vec3 TraceRadiance(const TraceContext& context, const Vec3& origin, const Vec3& direction, Pcg32& rng);

  } // namespace LightProbeBake
} // namespace ToolKit
