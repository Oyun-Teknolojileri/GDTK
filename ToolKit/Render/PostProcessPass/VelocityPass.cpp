/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "VelocityPass.h"

#include "Framebuffer.h"
#include "Material.h"
#include "Texture.h"
#include "Renderer.h"
#include "Shader.h"
#include "ToolKit.h"

namespace ToolKit
{

  namespace
  {
    /** Difference below this is float noise in a matrix that did not move, not motion. */
    constexpr float g_motionEpsilon = 1e-4f;

    bool MatrixChanged(const Mat4& a, const Mat4& b)
    {
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          if (glm::abs(a[column][row] - b[column][row]) > g_motionEpsilon)
          {
            return true;
          }
        }
      }

      return false;
    }

    TextureSettings VelocitySettings()
    {
      TextureSettings settings;
      settings.WarpS          = GraphicTypes::UVClampToEdge;
      settings.WarpT          = GraphicTypes::UVClampToEdge;
      settings.InternalFormat = GraphicTypes::FormatRGBA16F;
      settings.Format         = GraphicTypes::FormatRGBA;
      settings.Type           = GraphicTypes::TypeFloat;
      settings.MinFilter      = GraphicTypes::SampleNearest;
      settings.MagFilter      = GraphicTypes::SampleNearest;
      settings.GenerateMipMap = false;
      return settings;
    }
  } // namespace

  VelocityPass::VelocityPass() : Pass("VelocityPass")
  {
    m_currentRt  = MakeNewPtr<RenderTarget>("VelocityCurrentRT");
    m_previousRt = MakeNewPtr<RenderTarget>("VelocityPreviousRT");
    m_currentFb  = MakeNewPtr<Framebuffer>("VelocityCurrentFB");
    m_previousFb = MakeNewPtr<Framebuffer>("VelocityPreviousFB");

    m_vertexShader =
        GetShaderManager()->Create<Shader>(ShaderPath("velocityVert.shader", true));
    m_fragmentShader =
        GetShaderManager()->Create<Shader>(ShaderPath("velocityFrag.shader", true));

    m_material = MakeNewPtr<Material>();
    m_material->SetVertexShaderVal(m_vertexShader);
    m_material->SetFragmentShaderVal(m_fragmentShader);
    m_material->Init();

  }

  VelocityPass::~VelocityPass()
  {
    m_currentRt    = nullptr;
    m_previousRt   = nullptr;
    m_currentFb    = nullptr;
    m_previousFb   = nullptr;
    m_material     = nullptr;
    m_vertexShader = nullptr;
    m_fragmentShader = nullptr;
  }

  TexturePtr VelocityPass::GetCurrentVelocity() const { return m_currentRt; }

  TexturePtr VelocityPass::GetPreviousVelocity() const { return m_previousRt; }

  void VelocityPass::PreRender()
  {
    Pass::PreRender();

    if (m_params.RenderJobs == nullptr || m_params.RenderSize.x == 0 || m_params.RenderSize.y == 0)
    {
      return;
    }

    const TextureSettings settings = VelocitySettings();
    m_currentRt->Settings(settings);
    m_previousRt->Settings(settings);
    m_currentRt->ReconstructIfNeeded(m_params.RenderSize.x, m_params.RenderSize.y);
    m_previousRt->ReconstructIfNeeded(m_params.RenderSize.x, m_params.RenderSize.y);

    const int width  = (int) m_params.RenderSize.x;
    const int height = (int) m_params.RenderSize.y;

    m_currentFb->ReconstructIfNeeded({width, height, false, false});
    m_previousFb->ReconstructIfNeeded({width, height, false, false});

    // The attachments are wired here, not in the constructor: a framebuffer that has not been built yet
    // has no GPU data to attach a target to.
    m_currentFb->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_currentRt);
    m_previousFb->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_previousRt);

    // Sorted into the two draws: a job whose transform moved since the cache was written is drawn with
    // this frame's transform and with the cached one. Everything else contributes nothing, and the
    // cleared targets already say so.
    m_currentJobs.clear();
    m_previousJobs.clear();

    for (const RenderJob& job : *m_params.RenderJobs)
    {
      if (job.Entity == nullptr || job.Mesh == nullptr)
      {
        continue;
      }

      auto cached = m_previousTransforms.find(job.Entity);
      if (cached == m_previousTransforms.end())
      {
        m_previousTransforms[job.Entity] = job.WorldTransform;
        continue;
      }

      if (!MatrixChanged(cached->second, job.WorldTransform))
      {
        continue;
      }

      RenderJob current = job;
      current.Material  = m_material.get();
      m_currentJobs.push_back(current);

      RenderJob previous = job;
      previous.Material  = m_material.get();
      previous.WorldTransform = cached->second;
      m_previousJobs.push_back(previous);

      cached->second = job.WorldTransform;
    }
  }

  void VelocityPass::Render()
  {
    if (m_params.RenderJobs == nullptr)
    {
      return;
    }

    Renderer* renderer = GetRenderer();

    RenderState state;
    state.depthTestEnabled  = false;
    state.depthWriteEnabled = false;
    state.cullMode          = CullingType::TwoSided;

    renderer->SetPassState(state);

    // Cleared even when nothing moved: a target that keeps the motion of the last frame that had one
    // moves the history of every pixel that reads it by an old value, which is a whole screen of ghost
    // that never goes away.
    // Cleared to zero, not to the frame's clear colour: the resolve subtracts the two targets, and a
    // background of one would read as motion on every silhouette edge.
    renderer->SetFramebuffer(m_currentFb, GraphicBitFields::None);
    renderer->ClearColorBuffer(Vec4(0.0f));

    if (!m_currentJobs.empty())
    {
      renderer->Render(m_currentJobs);
    }

    renderer->SetFramebuffer(m_previousFb, GraphicBitFields::None);
    renderer->ClearColorBuffer(Vec4(0.0f));

    if (!m_previousJobs.empty())
    {
      renderer->Render(m_previousJobs);
    }
  }

} // namespace ToolKit
