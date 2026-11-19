/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "BakeScene.h"

#include "DirectionComponent.h"
#include "EnvironmentComponent.h"
#include "GradientSky.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshComponent.h"
#include "Scene.h"
#include "Texture.h"

#include <cmath>
#include <unordered_map>

namespace ToolKit
{
  namespace LightProbeBake
  {
    namespace
    {
      /** Last resort albedo for a mesh with no material assigned. */
      constexpr float DefaultAlbedo = 0.8f;

      /** Lights fainter than this contribute nothing worth a shadow ray. */
      constexpr float MinLightIntensity = 1e-5f;

      /**
       * Mean colour of a texture, in the linear space the GPU samples it in.
       *
       * A probe can not resolve what a map paints over a surface, only the radiance that surface
       * carries per unit area, which is the map's mean. Textures load as sRGB (FormatSRGB8_A8 is
       * the default, and the GPU linearizes it on sample), so the mean is taken after the same
       * conversion: a mostly black emissive map with a few bright panes would otherwise average
       * far brighter than it renders.
       */
      bool AverageTextureColor(const TexturePtr& texture, Vec3& out)
      {
        if (texture == nullptr)
        {
          return false;
        }

        // The bake runs headless, so the pixels are asked for explicitly: a material that was
        // never drawn has not touched its textures yet.
        if (texture->m_image == nullptr && texture->m_imagef == nullptr &&
            texture->m_compressedLevels.empty())
        {
          texture->Load();
        }

        const size_t texelCount = (size_t) texture->m_width * (size_t) texture->m_height;
        if (texelCount == 0)
        {
          return false;
        }

        // A component count below three means a grey map; replicate the first channel rather than
        // reading past the pixel.
        const int channels = glm::max(texture->m_numChannels, 1);
        Vec3 sum(0.0f);

        if (texture->m_imagef != nullptr)
        {
          for (size_t i = 0; i < texelCount; ++i)
          {
            const float* pixel = texture->m_imagef + i * channels;
            sum += Vec3(pixel[0],
                        channels > 1 ? pixel[1] : pixel[0],
                        channels > 2 ? pixel[2] : pixel[0]);
          }
          out = sum / (float) texelCount;
          return true;
        }

        if (texture->m_image == nullptr)
        {
          // Block compressed levels have no CPU side pixels to average; the caller falls back to
          // the material's colour, which is what an untextured material contributes anyway.
          return false;
        }

        // 8 bit sRGB to linear, once per byte value: a pow() per channel per texel is otherwise
        // the entire cost of this function on a 2k map.
        static float srgbToLinear[256];
        static bool tableBuilt = false;
        if (!tableBuilt)
        {
          for (int i = 0; i < 256; ++i)
          {
            const float c   = (float) i / 255.0f;
            srgbToLinear[i] = c <= 0.04045f ? c / 12.92f : (float) std::pow((c + 0.055) / 1.055, 2.4);
          }
          tableBuilt = true;
        }

        for (size_t i = 0; i < texelCount; ++i)
        {
          const ubyte* pixel = texture->m_image + i * channels;
          sum += Vec3(srgbToLinear[pixel[0]],
                      srgbToLinear[channels > 1 ? pixel[1] : pixel[0]],
                      srgbToLinear[channels > 2 ? pixel[2] : pixel[0]]);
        }

        out = sum / (float) texelCount;
        return true;
      }

      uint MaterialIndex(BakeScene& scene,
                           std::unordered_map<const Material*, uint>& cache,
                           const MaterialPtr& material)
      {
        if (material == nullptr)
        {
          return 0;
        }

        const auto itr = cache.find(material.get());
        if (itr != cache.end())
        {
          return itr->second;
        }

        SurfaceMaterial surface;

        // The forward shader reads a diffuse or emissive map *instead of* the matching colour
        // (defaultFragment.shader: texture(s_diffuseColor) / texture(s_emissiveColor)), so the bake
        // mirrors that instead of adding the two. This is what makes a night city work: the lit
        // windows live in an emissive map next to a black EmissiveColor, and reading the colour
        // alone traced a city with no light in it.
        Vec3 average;
        if (AverageTextureColor(material->GetDiffuseTextureVal(), average))
        {
          surface.albedo = average;
        }
        else
        {
          surface.albedo = material->GetColorVal();
        }

        if (AverageTextureColor(material->GetEmissiveTextureVal(), average))
        {
          surface.emissive = average;
        }
        else
        {
          surface.emissive = material->GetEmissiveColorVal();
        }

        // The frame draws a blended material over what is behind it, so a glass facade is not a
        // wall: light goes through it in the bake too. Additive materials (a glow, a light cone)
        // are in the same group and only ever emit.
        surface.passesLight = material->IsTranslucent();

        const uint index      = (uint) scene.materials.size();
        scene.materials.push_back(surface);
        cache[material.get()]   = index;

        // One line per material, because "the bake is dark" is the question this data answers.
        TK_LOG("Probe bake material '%s': albedo %.3f %.3f %.3f, emissive %.3f %.3f %.3f%s.",
               material->GetFile().c_str(),
               surface.albedo.x,
               surface.albedo.y,
               surface.albedo.z,
               surface.emissive.x,
               surface.emissive.y,
               surface.emissive.z,
               surface.passesLight ? ", light passes through" : "");

        return index;
      }
    } // namespace

    bool BuildBakeScene(Scene* scene, BakeScene& out, String& error)
    {
      out = BakeScene {};

      if (scene == nullptr)
      {
        error = "No scene to bake.";
        return false;
      }

      // Index 0 is the fallback every mesh without a material points at.
      SurfaceMaterial fallback;
      fallback.albedo = Vec3(DefaultAlbedo);
      out.materials.push_back(fallback);

      std::unordered_map<const Material*, uint> materialCache;
      std::vector<Triangle> triangles;

      for (const EntityPtr& entityPtr : scene->GetEntities())
      {
        Entity* entity = entityPtr.get();
        if (entity == nullptr || !entity->IsVisible())
        {
          continue;
        }

        MeshComponent* meshComp = entity->GetComponentFast<MeshComponent>();
        if (meshComp == nullptr || meshComp->GetMeshVal() == nullptr)
        {
          continue;
        }

        const Mat4 world        = entity->m_node->GetTransform(TransformationSpace::TS_WORLD);
        const Mat3 normalMatrix = glm::transpose(glm::inverse(Mat3(world)));

        for (Mesh* mesh : meshComp->GetMeshVal()->GetAllMeshes())
        {
          if (mesh == nullptr)
          {
            continue;
          }

          const VertexArray& vertices = mesh->m_clientSideVertices;
          if (vertices.size() < 3)
          {
            continue;
          }

          const uint material = MaterialIndex(out, materialCache, mesh->m_material);

          auto addTriangle = [&](const Vertex& a, const Vertex& b, const Vertex& c)
          {
            Triangle tri;
            tri.p0 = Vec3(world * Vec4(a.pos, 1.0f));
            tri.p1 = Vec3(world * Vec4(b.pos, 1.0f));
            tri.p2 = Vec3(world * Vec4(c.pos, 1.0f));

            tri.n0 = glm::normalize(normalMatrix * a.norm);
            tri.n1 = glm::normalize(normalMatrix * b.norm);
            tri.n2 = glm::normalize(normalMatrix * c.norm);

            // A degenerate normal is worse than a wrong one: it poisons every bounce.
            const Vec3 faceNormal = glm::cross(tri.p1 - tri.p0, tri.p2 - tri.p0);
            const float area      = glm::length(faceNormal);
            if (area < 1e-8f)
            {
              return;
            }

            if (glm::length(tri.n0) < 0.5f)
            {
              tri.n0 = faceNormal / area;
            }
            if (glm::length(tri.n1) < 0.5f)
            {
              tri.n1 = faceNormal / area;
            }
            if (glm::length(tri.n2) < 0.5f)
            {
              tri.n2 = faceNormal / area;
            }

            tri.material = material;
            triangles.push_back(tri);
          };

          if (mesh->m_clientSideIndices.empty())
          {
            // Index free mesh: the vertices are a triangle soup.
            for (size_t i = 0; i + 2 < vertices.size(); i += 3)
            {
              addTriangle(vertices[i], vertices[i + 1], vertices[i + 2]);
            }
          }
          else
          {
            const auto& indices = mesh->m_clientSideIndices;
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
              const size_t i0 = indices[i];
              const size_t i1 = indices[i + 1];
              const size_t i2 = indices[i + 2];
              if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
              {
                continue;
              }
              addTriangle(vertices[i0], vertices[i1], vertices[i2]);
            }
          }
        }
      }

      // Lights.
      for (Light* light : scene->GetLights())
      {
        if (light == nullptr || light->GetIntensityVal() <= MinLightIntensity)
        {
          continue;
        }

        SceneLight outLight;
        outLight.color     = light->GetColorVal();
        outLight.intensity = light->GetIntensityVal();
        outLight.position  = light->m_node->GetTranslation(TransformationSpace::TS_WORLD);

        if (DirectionComponent* direction = light->GetComponentFast<DirectionComponent>())
        {
          outLight.direction = direction->GetDirection();
        }

        switch (light->GetLightType())
        {
          case Light::LightType::Directional:
          {
            outLight.kind = LightKind::Directional;
            break;
          }
          case Light::LightType::Point:
          {
            outLight.kind   = LightKind::Point;
            outLight.radius = static_cast<PointLight*>(light)->GetRadiusVal();
            break;
          }
          case Light::LightType::Spot:
          {
            SpotLight* spot   = static_cast<SpotLight*>(light);
            outLight.kind     = LightKind::Spot;
            outLight.radius   = spot->GetRadiusVal();
            outLight.cosInner = std::cos(glm::radians(spot->GetInnerAngleVal()));
            outLight.cosOuter = std::cos(glm::radians(spot->GetOuterAngleVal()));
            break;
          }
        }

        out.lights.push_back(outLight);
      }

      // Sky.
      if (SkyBasePtr sky = scene->GetSky())
      {
        if (GradientSky* gradient = sky->As<GradientSky>())
        {
          out.sky.top      = gradient->GetTopColorVal();
          out.sky.middle   = gradient->GetMiddleColorVal();
          out.sky.bottom   = gradient->GetBottomColorVal();
          out.sky.exponent = glm::max(gradient->GetGradientExponentVal(), 0.01f);
          out.sky.enabled  = true;

          if (EnvironmentComponent* env = sky->GetComponentFast<EnvironmentComponent>())
          {
            if (!env->GetIlluminateVal())
            {
              out.sky.enabled = false;
            }
            else
            {
              const float intensity = env->GetIntensityVal();
              out.sky.top *= intensity;
              out.sky.middle *= intensity;
              out.sky.bottom *= intensity;
            }
          }
        }
      }

      if (triangles.empty())
      {
        error = "The scene has no visible static geometry to bake.";
        return false;
      }

      out.bvh.Build(std::move(triangles));
      out.boundsMin = out.bvh.BoundsMin();
      out.boundsMax = out.bvh.BoundsMax();

      // Flattened once: a shadow ray asks the Bvh, which knows a material index and nothing else.
      out.materialsPassingLight.resize(out.materials.size(), 0);
      for (size_t i = 0; i < out.materials.size(); ++i)
      {
        out.materialsPassingLight[i] = out.materials[i].passesLight ? 1 : 0;
      }

      return true;
    }

  } // namespace LightProbeBake
} // namespace ToolKit
