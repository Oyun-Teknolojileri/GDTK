/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "PathTracer.h"

#include <algorithm>

namespace ToolKit
{
  namespace LightProbeBake
  {
    namespace
    {
      constexpr float Pi = 3.14159265359f;

      /** Offset along the shading normal before the next ray, to clear the surface itself. */
      constexpr float ShadowEpsilon = 1e-3f;

      /** Keeps one stray sample from carrying the whole probe. */
      constexpr float MaxRadiance = 1.0e5f;

      /** Distance a ray is allowed to travel. The engine has no infinite scenes. */
      constexpr float RayFar = 1.0e7f;

      /**
       * A shadow ray starts on the surface and ends at the light, so the epsilon is applied to
       * both ends. Skipping it at the far end makes the surface that carries the light block its
       * own contribution.
       */
      constexpr float ShadowRayShrink = 2.0f * ShadowEpsilon;

      /**
       * How many blended surfaces one path may walk through before it gives up. Glass facades
       * stack two or three deep; the cap is there so a degenerate geometry can not spin the loop.
       */
      constexpr int MaxPassThroughs = 16;

      /** Mirrors DistanceAttenuation in lighting.shader. */
      inline float DistanceAttenuation(float distanceSquared, float radius)
      {
        const float factor = glm::clamp(distanceSquared / (radius * radius), 0.0f, 1.0f);
        const float falloff = 1.0f - factor;
        return falloff * falloff;
      }

      /** Mirrors SpotAngleAttenuation in lighting.shader. */
      inline float SpotAttenuation(float cosAngle, float cosInner, float cosOuter)
      {
        const float scale  = 1.0f / glm::max(cosInner - cosOuter, 1e-4f);
        const float offset = -cosOuter * scale;
        const float atten  = glm::clamp(cosAngle * scale + offset, 0.0f, 1.0f);
        return atten * atten;
      }

      /**
       * Samples one light at `position` with the shading normal `normal`.
       * @return False when the light can not reach the point at all.
       */
      bool SampleLight(const SceneLight& light,
                       const Vec3& position,
                       const Vec3& normal,
                       Vec3& directionToLight,
                       float& distance,
                       Vec3& radiance)
      {
        switch (light.kind)
        {
          case LightKind::Directional:
          {
            directionToLight = glm::normalize(-light.direction);
            distance         = RayFar;
            radiance         = light.color * light.intensity;
            return glm::dot(normal, directionToLight) > 0.0f;
          }
          case LightKind::Point:
          case LightKind::Spot:
          {
            const Vec3 toLight = light.position - position;
            const float distanceSquared = glm::dot(toLight, toLight);
            if (light.radius <= 0.0f || distanceSquared >= light.radius * light.radius)
            {
              return false;
            }

            distance         = std::sqrt(distanceSquared);
            directionToLight = toLight / glm::max(distance, 1e-6f);

            if (glm::dot(normal, directionToLight) <= 0.0f)
            {
              return false;
            }

            float attenuation = DistanceAttenuation(distanceSquared, light.radius);
            if (light.kind == LightKind::Spot)
            {
              const float cosAngle = glm::dot(-directionToLight, glm::normalize(light.direction));
              attenuation *= SpotAttenuation(cosAngle, light.cosInner, light.cosOuter);
            }

            if (attenuation <= 1e-6f)
            {
              return false;
            }

            radiance = light.color * (light.intensity * attenuation);
            return true;
          }
        }

        return false;
      }
    } // namespace

    Vec3 TraceRadiance(const TraceContext& context, const Vec3& origin, const Vec3& direction, Pcg32& rng)
    {
      const BakeScene* scene = context.scene;
      // An empty scene is legal: every path misses and only the sky contributes, which is what
      // the self test in the ProbeBake tool relies on.
      if (scene == nullptr)
      {
        return Vec3(0.0f);
      }

      Vec3 radiance(0.0f);
      Vec3 throughput(1.0f);

      Vec3 rayOrigin    = origin;
      Vec3 rayDirection = glm::normalize(direction);
      int passThroughs  = 0;

      for (int bounce = 0; bounce <= context.maxBounces; ++bounce)
      {
        RayHit hit;
        if (!scene->bvh.Intersect(rayOrigin, rayDirection, RayFar, hit))
        {
          radiance += throughput * scene->sky.Sample(rayDirection);
          break;
        }

        const std::vector<Triangle>& triangles = scene->bvh.Triangles();
        const Triangle& tri                    = triangles[hit.triangle];
        const SurfaceMaterial& material        = scene->materials[glm::min(tri.material,
                                                                     (uint) scene->materials.size() - 1)];

        const Vec3 position = rayOrigin + rayDirection * hit.t;

        // A blended surface neither blocks the path nor bounces it: the ray carries on straight
        // through, and the surface still emits, so a lit window behind glass reaches the probe.
        // This is not a bounce, so the loop counter does not advance.
        if (material.passesLight)
        {
          radiance += throughput * material.emissive;

          if (++passThroughs > MaxPassThroughs)
          {
            break;
          }

          rayOrigin = position + rayDirection * ShadowEpsilon;
          --bounce;
          continue;
        }

        Vec3 normal = tri.n0 * (1.0f - hit.u - hit.v) + tri.n1 * hit.u + tri.n2 * hit.v;
        if (glm::length(normal) < 1e-6f)
        {
          normal = tri.n0;
        }
        normal = glm::normalize(normal);

        // Two sided shading: a wall seen from inside the room is still the same wall.
        if (glm::dot(normal, rayDirection) > 0.0f)
        {
          normal = -normal;
        }

        radiance += throughput * material.emissive;

        // Next event estimation: sample the lights directly, so a small emitter is not missed.
        for (const SceneLight& light : scene->lights)
        {
          Vec3 directionToLight(0.0f);
          float distance = 0.0f;
          Vec3 lightRadiance(0.0f);

          if (!SampleLight(light, position, normal, directionToLight, distance, lightRadiance))
          {
            continue;
          }

          const float shadowTMax = glm::min(distance - ShadowRayShrink, RayFar);
          if (shadowTMax > ShadowEpsilon &&
              scene->bvh.Occluded(position + normal * ShadowEpsilon,
                                  directionToLight,
                                  shadowTMax,
                                  &scene->materialsPassingLight))
          {
            continue;
          }

          const float cosTheta = glm::max(glm::dot(normal, directionToLight), 0.0f);
          radiance += throughput * material.albedo * (cosTheta / Pi) * lightRadiance;
        }

        // Continue the path with a cosine weighted diffuse bounce.
        throughput *= material.albedo;
        rayOrigin    = position + normal * ShadowEpsilon;
        rayDirection = rng.SampleCosineHemisphere(normal);

        if (bounce >= 2)
        {
          const float survival = glm::clamp(glm::max(glm::max(throughput.x, throughput.y), throughput.z),
                                            0.05f,
                                            0.95f);
          if (rng.NextFloat() > survival)
          {
            break;
          }
          throughput /= survival;
        }
      }

      // A non finite sample poisons every coefficient it lands in, and the shader interpolates a
      // NaN probe across the whole cell it shares with its neighbours - the volume goes dark as if
      // the bake had never run. One wrong probe is a bug; a NaN probe is a dead volume.
      if (!glm::all(glm::isfinite(radiance)))
      {
        return Vec3(0.0f);
      }

      const float magnitude = glm::max(glm::max(radiance.x, radiance.y), radiance.z);
      if (magnitude > MaxRadiance)
      {
        radiance *= MaxRadiance / magnitude;
      }

      return glm::max(radiance, Vec3(0.0f));
    }

  } // namespace LightProbeBake
} // namespace ToolKit
