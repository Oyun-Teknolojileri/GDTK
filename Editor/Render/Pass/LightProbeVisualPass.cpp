/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, please visit [otyazilim.com].
 */

#include "LightProbeVisualPass.h"

#include "Material.h"
#include "Mesh.h"
#include "MeshComponent.h"
#include "Primative.h"
#include "Shader.h"
#include "Texture.h"
#include "ToolKit.h"

#include <algorithm>
#include <cstring>

#include <DebugNew.h>

namespace ToolKit
{
  namespace Editor
  {

    namespace
    {
      /** Texels per row of the sample table; must match SAMPLE_TEX_WIDTH in the vertex shader. */
      constexpr int SampleTableWidth = 256;

      /** Texels one point occupies: its position and radius, then its colour. */
      constexpr int TexelsPerPoint = 2;
    } // namespace

    LightProbeVisualPass::LightProbeVisualPass() : Pass("LightProbeVisualPass")
    {
      // Solid markers among the editor objects: they write depth so a point behind the camera is
      // not blended over the ones in front of it. Two sided, because a marker is a dot seen from
      // any angle and its winding is not worth a culled visual.
      m_passState.depthTestEnabled  = true;
      m_passState.depthWriteEnabled = true;
      m_passState.cullMode          = CullingType::TwoSided;

      // One sphere, reused by every instance. Six rings is plenty for a five centimetre marker and
      // keeps the triangle count honest: a grid of two thousand points at the primitive's thirty
      // two by thirty two would be four million triangles for dots the size of a pebble.
      MeshComponentPtr meshComp = MakeNewPtr<MeshComponent>();
      Sphere::Generate(meshComp, 1.0f, 6, 6);
      m_sphereMesh = meshComp->GetMeshVal();

      m_material = MakeNewPtr<Material>();
      m_material->SetVertexShaderVal(
          GetShaderManager()->Create<Shader>(ShaderPath("lightProbeVisVert.shader", true)));
      m_material->SetFragmentShaderVal(
          GetShaderManager()->Create<Shader>(ShaderPath("lightProbeVisFrag.shader", true)));
      m_material->cullMode = CullingType::TwoSided;
      m_material->Init();
    }

    void LightProbeVisualPass::PreRender()
    {
      Pass::PreRender();

      m_pointCount = (uint) (m_params.Samples.size() / TexelsPerPoint);
      if (m_pointCount == 0)
      {
        return;
      }

      m_material->Init();
      if (m_sphereMesh != nullptr)
      {
        m_sphereMesh->Init();
      }

      UpdateTable();
    }

    void LightProbeVisualPass::UpdateTable()
    {
      const int texelCount = (int) m_params.Samples.size();
      const int rows       = glm::max(1, (texelCount + SampleTableWidth - 1) / SampleTableWidth);
      const size_t floats  = (size_t) rows * (size_t) SampleTableWidth * 4;

      // Every frame is rebuilt rather than diffed: the points move with the probe and the values
      // are a few hundred kilobytes at most, while a comparison would walk the same data anyway.
      const bool recreate = m_table == nullptr || m_upload.size() != floats;

      m_upload.assign(floats, 0.0f);
      std::memcpy(m_upload.data(), m_params.Samples.data(), m_params.Samples.size() * sizeof(Vec4));

      if (recreate)
      {
        TextureSettings settings;
        settings.Target         = GraphicTypes::Target2D;
        settings.WarpS          = GraphicTypes::UVClampToEdge;
        settings.WarpT          = GraphicTypes::UVClampToEdge;
        settings.MinFilter      = GraphicTypes::SampleNearest;
        settings.MagFilter      = GraphicTypes::SampleNearest;
        settings.InternalFormat = GraphicTypes::FormatRGBA32F;
        settings.Format         = GraphicTypes::FormatRGBA;
        settings.Type           = GraphicTypes::TypeFloat;

        m_table = MakeNewPtr<DataTexture>(SampleTableWidth, rows, settings, "LightProbeSampleTable");
        m_table->Init(m_upload.data());
      }
      else
      {
        m_table->Map(m_upload.data());
      }

      m_material->SetDiffuseTextureVal(m_table);
    }

    void LightProbeVisualPass::Render()
    {
      Renderer* renderer = GetRenderer();
      if (renderer == nullptr || m_params.Cam == nullptr || m_pointCount == 0 || m_table == nullptr)
      {
        return;
      }

      renderer->SetFramebuffer(m_params.FrameBuffer, GraphicBitFields::None);
      renderer->SetCamera(m_params.Cam, true);
      renderer->SetPassState(m_passState);

      RenderJob job;
      job.Mesh           = m_sphereMesh.get();
      job.Material       = m_material.get();
      job.WorldTransform = Mat4(1.0f);

      renderer->BindProgramOfMaterial(m_material.get());
      renderer->RenderBatch(job, m_pointCount, 0);
    }

    void LightProbeVisualPass::PostRender() { Pass::PostRender(); }

  } // namespace Editor
} // namespace ToolKit
