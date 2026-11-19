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
        surface.albedo   = material->GetColorVal();
        surface.emissive = material->GetEmissiveColorVal();

        const uint index      = (uint) scene.materials.size();
        scene.materials.push_back(surface);
        cache[material.get()]   = index;

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

      return true;
    }

  } // namespace LightProbeBake
} // namespace ToolKit
