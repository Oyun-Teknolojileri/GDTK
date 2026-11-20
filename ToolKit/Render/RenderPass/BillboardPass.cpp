/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "BillboardPass.h"

#include "Entity.h"
#include "Material.h"
#include "Shader.h"

#include <algorithm>

#include <DebugNew.h>

namespace ToolKit
{
  namespace
  {
    /**
     * Alpha masked materials discard in the fragment shader, and the discard is compiled in through
     * the DrawAlphaMasked define. The forward pass sets that define per draw range; a billboard
     * carries its blend state on its material, so each job selects its own variant.
     * @return Shader whose define was set, null when the shader does not declare the define.
     */
    ShaderPtr SetAlphaMaskVariant(Material* material)
    {
      ShaderPtr frag = material->GetFragmentShaderVal();
      if (frag == nullptr)
      {
        return nullptr;
      }

      const auto defineItr = std::find_if(frag->m_defineArray.begin(),
                                          frag->m_defineArray.end(),
                                          [](const ShaderDefine& def) { return def.define == "DrawAlphaMasked"; });
      if (defineItr == frag->m_defineArray.end())
      {
        return nullptr;
      }

      frag->SetDefine("DrawAlphaMasked", material->IsAlphaMasked() ? "1" : "0");
      return frag;
    }
  } // namespace

  BillboardPass::BillboardPass() : Pass("BillboardPass") {}

  void BillboardPass::Render()
  {
    Renderer* renderer = GetRenderer();
    Viewport* vp       = m_params.Viewport;

    // The overlay target the editor hands in, or the viewport's own frame buffer.
    FramebufferPtr target = m_params.FrameBuffer ? m_params.FrameBuffer : vp->m_framebuffer;

    renderer->SetFramebuffer(target, GraphicBitFields::None);
    CameraPtr cam = vp->GetCamera();
    renderer->SetCamera(cam, true);

    ShaderPtr maskVariantShader = nullptr;

    auto renderBillboardsFn = [this, renderer, &maskVariantShader](EntityPtrArray& billboards, bool depthTest) -> void
    {
      m_renderData.jobs.clear();

      EntityRawPtrArray rawBillboards = ToEntityRawPtrArray(billboards);
      RenderJobProcessor::CreateRenderJobs(m_renderData.jobs, rawBillboards);
      RenderJobProcessor::SeperateRenderData(m_renderData, true);

      // depthTestEnabled is the only passive field that varies per billboard group.
      m_passState.depthTestEnabled = depthTest;
      renderer->SetPassState(m_passState);

      for (const RenderJob& job : m_renderData.jobs)
      {
        // The material has to be initiated before the shader accepts a define variant.
        job.Material->Init();
        if (ShaderPtr shader = SetAlphaMaskVariant(job.Material))
        {
          maskVariantShader = shader;
        }

        renderer->RenderWithProgramFromMaterial(job);
      }
    };

    renderBillboardsFn(m_noDepthBillboards, false);
    renderBillboardsFn(m_params.Billboards, true);

    // DrawAlphaMasked lives on the shared unlit shader, which every pass compiles its own variants
    // from. Leave it at the value the rest of the frame expects instead of the last billboard's.
    if (maskVariantShader != nullptr)
    {
      maskVariantShader->SetDefine("DrawAlphaMasked", "0");
    }
  }

  void BillboardPass::PreRender()
  {
    Pass::PreRender();

    // Process billboards.
    float vpScale = m_params.Viewport->GetBillboardScale();
    CameraPtr cam = m_params.Viewport->GetCamera();
    m_noDepthBillboards.clear();

    // Separate functions that does not require depth test.
    move_values(m_params.Billboards,
                m_noDepthBillboards,
                [this, vpScale, cam](EntityPtr bb) -> bool
                {
                  // Update billboards.
                  BillboardPtr cbb = Cast<Billboard>(bb);
                  cbb->LookAt(cam, vpScale);

                  // Return separation condition.
                  return cbb->m_settings.bypassDepthTest;
                });
  }

  void BillboardPass::PostRender()
  {
    Pass::PostRender();
    GetRenderer()->FinishPass();
  }

} // namespace ToolKit
