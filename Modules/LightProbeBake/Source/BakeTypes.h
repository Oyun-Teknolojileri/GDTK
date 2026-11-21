/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file BakeTypes.h
 *
 * Plain data the ray tracer works on. Nothing here points at an engine object, which is what
 * lets the bake run on worker threads while the editor keeps drawing and the scene keeps
 * being edited.
 */

#include "Types.h"

#include <cmath>
#include <cfloat>
#include <vector>

namespace ToolKit
{
  namespace LightProbeBake
  {

    /** A world space triangle with interpolated vertex normals. */
    struct Triangle
    {
      Vec3 p0 = Vec3(0.0f);
      Vec3 p1 = Vec3(0.0f);
      Vec3 p2 = Vec3(0.0f);
      Vec3 n0 = Vec3(0.0f, 1.0f, 0.0f);
      Vec3 n1 = Vec3(0.0f, 1.0f, 0.0f);
      Vec3 n2 = Vec3(0.0f, 1.0f, 0.0f);
      uint material = 0;

      Vec3 Centroid() const { return (p0 + p1 + p2) * (1.0f / 3.0f); }
    };

    /** What the baker needs to know about a surface. Textures are not sampled. */
    struct SurfaceMaterial
    {
      Vec3 albedo   = Vec3(0.8f);
      Vec3 emissive = Vec3(0.0f);

      /**
       * A blended surface (glass, water, an additive glow) neither blocks the path nor bounces
       * it: the ray continues straight through and the surface still emits. Treating one as a
       * solid occluder is what turned a glass facade into a wall in the bake, darkening every
       * probe behind it.
       */
      bool passesLight = false;
    };

    enum class LightKind : uint8
    {
      Directional,
      Point,
      Spot
    };

    struct SceneLight
    {
      LightKind kind     = LightKind::Directional;
      Vec3 position      = Vec3(0.0f);

      /** The direction the light travels, not the direction towards it. */
      Vec3 direction     = Vec3(0.0f, -1.0f, 0.0f);
      Vec3 color         = Vec3(1.0f);
      float intensity    = 1.0f;
      float radius       = 0.0f;
      float cosInner     = 1.0f;
      float cosOuter     = 0.0f;
    };

    /**
     * The sky a ray sees when it misses the geometry.
     *
     * Two forms share the struct: the analytic vertical gradient of a GradientSky, and an
     * equirectangular HDRI image resampled on the CPU. The HDRI is kept as plain float texels so
     * the snapshot still owns no engine object.
     *
     * The image is sampled the way the renderer converts it for the GPU
     * (equirectToCubeFrag.shader): the same equirectangular mapping, the same `1 - exp(-c)` curve
     * GenerateCubemapFrom2DTexture applies with an exposure of one, and the same sky orientation.
     * A sky that lit the frame with one curve and the probes with another would meet the probe
     * volume at a visible seam.
     */
    struct SkyModel
    {
      Vec3 top       = Vec3(0.0f);
      Vec3 middle    = Vec3(0.0f);
      Vec3 bottom    = Vec3(0.0f);
      float exponent = 1.0f;
      bool enabled   = false;

      /** Equirectangular HDRI, row major RGB float, row 0 at the top of the image. */
      std::vector<float> hdri;
      int hdriWidth  = 0;
      int hdriHeight = 0;

      /** Sky node orientation, the rotation the renderer applies to the IBL lookup direction. */
      Mat3 rotation = Mat3(1.0f);

      Vec3 Sample(const Vec3& direction) const;
    };

    inline Vec3 SkyModel::Sample(const Vec3& direction) const
    {
      if (!enabled)
      {
        return Vec3(0.0f);
      }

      const Vec3 dir = glm::normalize(rotation * direction);

      if (!hdri.empty())
      {
        // equirectToCubeFrag.shader: uv = (atan(z, x), asin(y)) * (1 / 2pi, 1 / pi) + 0.5.
        const float u = std::atan2(dir.z, dir.x) * 0.15915494f + 0.5f;
        const float v = std::asin(glm::clamp(dir.y, -1.0f, 1.0f)) * 0.31830989f + 0.5f;

        // v grows towards up while row 0 is the top of the image, so the row runs backwards. u
        // wraps around the seam at the back of the sphere; v clamps at the poles.
        const float x  = u * (float) hdriWidth - 0.5f;
        const float y  = (1.0f - v) * (float) hdriHeight - 0.5f;
        const int x0   = (int) std::floor(x);
        const int y0   = (int) std::floor(y);
        const float fx = x - (float) x0;
        const float fy = y - (float) y0;

        const auto texel = [this](int tx, int ty) -> Vec3
        {
          tx = ((tx % hdriWidth) + hdriWidth) % hdriWidth;
          ty = glm::clamp(ty, 0, hdriHeight - 1);
          const float* pixel = hdri.data() + ((size_t) ty * (size_t) hdriWidth + (size_t) tx) * 3;
          return Vec3(pixel[0], pixel[1], pixel[2]);
        };

        const Vec3 bottomRow = glm::mix(texel(x0, y0), texel(x0 + 1, y0), fx);
        const Vec3 topRow    = glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), fx);
        return glm::mix(bottomRow, topRow, fy);
      }

      const float y = glm::clamp(dir.y, -1.0f, 1.0f);
      if (y >= 0.0f)
      {
        return glm::mix(middle, top, std::pow(y, exponent));
      }
      return glm::mix(middle, bottom, std::pow(-y, exponent));
    }

    struct RayHit
    {
      float t          = FLT_MAX;
      uint triangle  = 0;
      float u          = 0.0f;
      float v          = 0.0f;
    };

    /**
     * PCG32. Seeded from the probe index, so the same scene bakes to the same bytes and two
     * bakes can be diffed.
     */
    class Pcg32
    {
     public:
      explicit Pcg32(uint64 seed = 0) { Reset(seed); }

      void Reset(uint64 seed)
      {
        m_state     = seed + 0x853c49e6748fea9bULL;
        m_increment = (seed << 1u | 1u) | 0xda3e39cb94b95bdbULL;
        NextUint();
      }

      uint NextUint()
      {
        const uint64 old = m_state;
        m_state          = old * 6364136223846793005ULL + m_increment;
        const uint xorshifted = (uint) (((old >> 18u) ^ old) >> 27u);
        const uint rot        = (uint) (old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
      }

      /** Uniform in [0, 1). */
      float NextFloat() { return (NextUint() >> 8) * (1.0f / 16777216.0f); }

      Vec2 NextFloat2() { return Vec2(NextFloat(), NextFloat()); }

      Vec3 SampleUniformSphere()
      {
        const float z    = 1.0f - 2.0f * NextFloat();
        const float r    = std::sqrt(glm::max(0.0f, 1.0f - z * z));
        const float phi  = 6.28318530718f * NextFloat();
        return Vec3(r * std::cos(phi), r * std::sin(phi), z);
      }

      /** Cosine weighted direction in the hemisphere around `normal`. */
      Vec3 SampleCosineHemisphere(const Vec3& normal)
      {
        const float u1 = NextFloat();
        const float u2 = NextFloat();
        const float r  = std::sqrt(u1);
        const float phi = 6.28318530718f * u2;

        Vec3 tangent;
        if (std::abs(normal.z) < 0.999f)
        {
          tangent = glm::normalize(glm::cross(Vec3(0.0f, 0.0f, 1.0f), normal));
        }
        else
        {
          tangent = glm::normalize(glm::cross(Vec3(0.0f, 1.0f, 0.0f), normal));
        }

        const Vec3 bitangent = glm::cross(normal, tangent);
        const Vec3 direction = tangent * (r * std::cos(phi)) + bitangent * (r * std::sin(phi)) +
                               normal * std::sqrt(glm::max(0.0f, 1.0f - u1));

        return glm::normalize(direction);
      }

     private:
      uint64 m_state     = 0;
      uint64 m_increment = 1;
    };

  } // namespace LightProbeBake
} // namespace ToolKit
