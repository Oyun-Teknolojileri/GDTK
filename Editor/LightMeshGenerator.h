/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include <Mesh.h>
#include <MeshComponent.h>
#include <Types.h>

namespace ToolKit
{
  namespace Editor
  {

    class LightMeshGenerator
    {
     public:
      LightMeshGenerator(Light* light);
      virtual ~LightMeshGenerator();

      virtual void InitGizmo() = 0;

      /**
       * Constructs a MeshComponent from the given lines. Destroys
       * all LineBatch objects in the lines array and clears the array.
       * @param lines is the line batch array that contains generated gizmo
       * data.
       */
      void TransferGizmoMesh(LineBatchPtrArray& lines);

     public:
      MeshComponentPtr m_lightMesh; //!< Component that contains generated data.

     protected:
      Light* m_targetLight = nullptr; //!< Light to generate mesh for.
    };

    class SpotLightMeshGenerator : public LightMeshGenerator
    {
     public:
      explicit SpotLightMeshGenerator(SpotLight* light);
      void InitGizmo() override;

     private:
      const int m_circleVertexCount = 36;

      Mat4 m_identityMatrix;
      Mat4 m_rot;

      Vec3 m_pnts[2];
      Vec3Array m_innerCirclePnts;
      Vec3Array m_outerCirclePnts;
      Vec3Array m_conePnts;
    };

    class DirectionalLightMeshGenerator : public LightMeshGenerator
    {
     public:
      explicit DirectionalLightMeshGenerator(DirectionalLight* light);
      void InitGizmo() override;

     private:
      Vec3Array m_pnts;
    };

    class PointLightMeshGenerator : public LightMeshGenerator
    {
     public:
      explicit PointLightMeshGenerator(PointLight* light);
      void InitGizmo() override;

     private:
      const int m_circleVertexCount = 30;

      Vec3Array m_circlePntsXY;
      Vec3Array m_circlePntsYZ;
      Vec3Array m_circlePntsXZ;
    };

  } // namespace Editor
} // namespace ToolKit