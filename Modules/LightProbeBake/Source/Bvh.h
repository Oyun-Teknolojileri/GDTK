/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file Bvh.h
 *
 * Static triangle BVH for the probe bake. `AABBTree` in the engine is an entity box tree used
 * for picking, not a triangle structure, so the baker carries its own.
 */

#include "BakeTypes.h"

#include <vector>

namespace ToolKit
{
  namespace LightProbeBake
  {

    class Bvh
    {
     public:
      /** Reorders the triangles it is given, so pass by value or move. */
      void Build(std::vector<Triangle>&& triangles);

      /** Nearest hit along the ray within `tMax`. */
      bool Intersect(const Vec3& origin, const Vec3& direction, float tMax, RayHit& hit) const;

      /**
       * Any opaquing hit along the ray within `tMax`.
       * @param passing One byte per material, non zero where light passes through. Such a triangle
       * is not an occluder, so the ray walks past a glass facade instead of stopping on it. Null
       * treats every triangle as opaque.
       */
      bool Occluded(const Vec3& origin,
                    const Vec3& direction,
                    float tMax,
                    const std::vector<ubyte>* passing = nullptr) const;

      const std::vector<Triangle>& Triangles() const { return m_triangles; }

      bool Empty() const { return m_triangles.empty(); }

      const Vec3& BoundsMin() const { return m_boundsMin; }
      const Vec3& BoundsMax() const { return m_boundsMax; }

     private:
      struct Node
      {
        Vec3 bmin        = Vec3(0.0f);
        Vec3 bmax        = Vec3(0.0f);
        /** Interior: index of the right child (left is nodeIndex + 1). Leaf: first triangle. */
        uint first     = 0;
        /** Zero for an interior node. */
        uint count     = 0;
      };

      uint BuildNode(uint begin, uint end, int depth);

      bool IntersectTriangle(uint triangle,
                             const Vec3& origin,
                             const Vec3& direction,
                             float& t,
                             float& u,
                             float& v) const;

      std::vector<Triangle> m_triangles;

      /** Build order. Leaves address this array, not m_triangles, so a build never moves 56 byte structs. */
      std::vector<uint> m_indices;
      std::vector<Vec3> m_centroids;
      std::vector<Node> m_nodes;

      Vec3 m_boundsMin = Vec3(0.0f);
      Vec3 m_boundsMax = Vec3(0.0f);
    };

  } // namespace LightProbeBake
} // namespace ToolKit
