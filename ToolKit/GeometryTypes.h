/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

/**
 * @file
 * This file contains declarations for various geometric shapes and related
 * utilities.
 */

#include "Types.h"

namespace ToolKit
{

  /**
   * A templated struct for a rectangle with X, Y, Width and Height.
   * @tparam T The type of the rectangle's components.
   */
  template <class T>
  struct Rect
  {
    T X;      //!< The X-coordinate of the rectangle.
    T Y;      //!< The Y-coordinate of the rectangle.
    T Width;  //!< The width of the rectangle.
    T Height; //!< The height of the rectangle.
  };

  /** Intersection result enum. */
  enum class IntersectResult
  {
    Outside,
    Inside,
    Intersect
  };

  /**
   *  A struct representing a bounding box in 3D space.
   */
  struct BoundingBox
  {
    Vec3 min = Vec3(TK_FLT_MAX);  //!< The minimum point of the bounding box.
    Vec3 max = Vec3(-TK_FLT_MAX); //!< The maximum point of the bounding box.

    BoundingBox() : min(TK_FLT_MAX), max(-TK_FLT_MAX) {}

    BoundingBox(Vec3 _min, Vec3 _max) : min(_min), max(_max) {}

    /**
     * Check if the bounding box is valid.
     * @return True if the bounding box is valid, false otherwise.
     */
    inline bool IsValid() const { return !glm::isinf(Volume()); }

    /**
     * Get the center point of the bounding box.
     * @return The center point of the bounding box.
     */
    inline Vec3 GetCenter() const { return min + (max - min) * 0.5f; }

    /**
     * Update the boundary of the bounding box with a new point.
     * @param v The new point to include in the bounding box.
     */
    inline void UpdateBoundary(const Vec3& v)
    {
      max = glm::max(max, v);
      min = glm::min(min, v);
    }

    /**
     * Update the boundary of the bounding box with another bounding box.
     * @param bb The bounding box to include in the bounding box.
     */
    inline void UpdateBoundary(const BoundingBox& bb)
    {
      UpdateBoundary(bb.max);
      UpdateBoundary(bb.min);
    }

    /** Creates a new bounding box that is union of the b1 and b2. */
    inline static BoundingBox Union(const BoundingBox& b1, const BoundingBox& b2)
    {
      return BoundingBox(glm::min(b1.min, b2.min), glm::max(b1.max, b2.max));
    }

    /**
     * Get the volume of the bounding box.
     * @return The volume of the bounding box.
     */
    inline float Volume() const { return glm::abs((max.x - min.x) * (max.y - min.y) * (max.z - min.z)); }

    /**
     * Calculates the half surface area of the bounding box.
     */
    inline float HalfSurfaceArea() const
    {
      const Vec3 e = max - min;
      return e.x * e.y + e.x * e.z + e.y * e.z;
    }

    /** Calculates surface area of the bounding box. */
    inline float SurfaceArea() const { return 2.0f * HalfSurfaceArea(); }

    /**
     * Get the width of the bounding box.
     * @return The width of the bounding box.
     */
    inline float GetWidth() const { return max.x - min.x; }

    /**
     * Get the height of the bounding box.
     * @return The height of the bounding box.
     */
    inline float GetHeight() const { return max.y - min.y; }

    /**
     * Get the depth of the bounding box.
     * @return The depth of the bounding box.
     */
    inline float GetDepth() const { return max.z - min.z; }
  };

  static const BoundingBox infinitesimalBox(Vec3(-TK_FLT_MIN), Vec3(TK_FLT_MIN));
  static const BoundingBox unitBox({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f});

  /**
   * A struct representing a ray in 3D space.
   */
  struct Ray
  {
    Vec3 position;  //!< The position of the ray.
    Vec3 direction; //!< The direction of the ray.
  };

  /**
   * A struct representing a plane equation in 3D space.
   * Plane equation: ax+by+cz+(-d)=0
   */
  struct PlaneEquation
  {
    Vec3 normal; //!< The normal vector of the plane.
    float d;     //!< Negated distance to the plane from origin.
  };

  /**
   * A struct representing a frustum in 3D space.
   * Expecting all plane normals to point inwards the frustum.
   */
  struct Frustum
  {
    PlaneEquation planes[6]; // Left - Right - Top - Bottom - Near - Far
  };

  /**
   * A struct representing a bounding sphere in 3D space.
   */
  struct BoundingSphere
  {
    Vec3 pos;     ///< The position of the center of the sphere.
    float radius; ///< The radius of the sphere.

    BoundingBox GetBoundingBox() const { return BoundingBox(pos - Vec3(radius), pos + Vec3(radius)); }
  };

} // namespace ToolKit