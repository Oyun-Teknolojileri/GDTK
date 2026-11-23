/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "EditorRenderer.h"

#include "App.h"
#include "EditorCanvas.h"
#include "EditorScene.h"
#include "EditorViewport.h"
#include "EditorViewport2d.h"
#include "Gizmo.h"
#include "Grid.h"
#include "LightMeshGenerator.h"

#include <Camera.h>
#include <DirectionComponent.h>
#include <EditorLightProbeVolumeComponent.h>
#include <EnvironmentComponent.h>
#include <GradientSky.h>
#include <LightProbe.h>
#include <LightProbeVolumeComponent.h>
#include <Material.h>
#include <MaterialComponent.h>
#include <MathUtil.h>
#include <Prefab.h>
#include <Primative.h>
#include <ReflectionProbe.h>
#include <Shader.h>
#include <UIManager.h>
#include <Util.h>

namespace ToolKit
{
  namespace Editor
  {

    EditorRenderer::EditorRenderer() { InitRenderer(); }

    EditorRenderer::EditorRenderer(const EditorRenderParams& params) : EditorRenderer() { m_params = params; }

    EditorRenderer::~EditorRenderer()
    {
      m_billboardPass   = nullptr;
      m_lightSystem     = nullptr;
      m_sceneRenderPath = nullptr;
      m_uiPass          = nullptr;
      m_editorPass      = nullptr;
      m_gizmoPass       = nullptr;
      m_outlinePass     = nullptr;
      m_probeVisual     = nullptr;
    }

    void EditorRenderer::PrepareOverlay(Renderer* renderer, Viewport* viewport)
    {
      if (viewport == nullptr || viewport->m_framebuffer == nullptr)
      {
        return;
      }

      const FramebufferSettings vpSettings = viewport->m_framebuffer->GetSettings();

      if (m_overlayFramebuffer == nullptr)
      {
        m_overlayFramebuffer = MakeNewPtr<Framebuffer>("EditorOverlayFB");
        m_overlayRt          = MakeNewPtr<RenderTarget>("EditorOverlayRT");
      }

      // Same size as the viewport and a single sample: the overlay is composited as color, so it needs no
      // coverage of its own, and its depth buffer only has to rank the overlays against each other.
      if (m_overlayRt->m_width != vpSettings.width || m_overlayRt->m_height != vpSettings.height)
      {
        TextureSettings colorSettings;
        colorSettings.InternalFormat = GraphicTypes::FormatRGBA8;
        colorSettings.Format         = GraphicTypes::FormatRGBA;
        colorSettings.Type           = GraphicTypes::TypeUnsignedByte;
        colorSettings.WarpS          = GraphicTypes::UVClampToEdge;
        colorSettings.WarpT          = GraphicTypes::UVClampToEdge;
        colorSettings.MinFilter      = GraphicTypes::SampleNearest;
        colorSettings.MagFilter      = GraphicTypes::SampleNearest;

        m_overlayRt->Settings(colorSettings);
        m_overlayRt->ReconstructIfNeeded(vpSettings.width, vpSettings.height);

        FramebufferSettings overlaySettings;
        overlaySettings.width     = vpSettings.width;
        overlaySettings.height    = vpSettings.height;
        overlaySettings.msaaCount = MsaaSampleCount::x0;

        m_overlayFramebuffer->ReconstructIfNeeded(overlaySettings);
        m_overlayFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_overlayRt);
      }

      // Cleared to nothing for the color, so what the composite blends in is exactly what the overlays drew,
      // and to the far plane for the depth, which is what orders them against one another.
      renderer->SetFramebuffer(m_overlayFramebuffer, GraphicBitFields::AllBits);
    }

    void EditorRenderer::CompositeOverlay(Renderer* renderer, Viewport* viewport)
    {
      if (m_overlayFramebuffer == nullptr || m_overlayRt == nullptr || viewport == nullptr)
      {
        return;
      }

      if (m_overlayMaterial == nullptr)
      {
        m_overlayMaterial = MakeNewPtr<Material>();
        m_overlayMaterial->SetVertexShaderVal(
            GetShaderManager()->Create<Shader>(ShaderPath("copyTextureVert.shader", true)));
        m_overlayMaterial->SetFragmentShaderVal(
            GetShaderManager()->Create<Shader>(ShaderPath("copyTextureFrag.shader", true)));
      }

      // A render target only ever written by a pass can carry mip filtering it does not have, and an
      // incomplete texture samples as zero: a black overlay rather than a transparent one.
      TextureSettings rtSettings = m_overlayRt->Settings();
      rtSettings.MinFilter       = GraphicTypes::SampleNearest;
      rtSettings.MagFilter       = GraphicTypes::SampleNearest;
      m_overlayRt->ApplyTextureSettings(rtSettings);

      m_overlayMaterial->SetDiffuseTextureVal(m_overlayRt);

      // Blend is an active bit and comes from the material, not from the pass state: the quad has to carry
      // the alpha blend itself, or it writes its cleared black over the scene as an opaque full screen copy.
      m_overlayMaterial->blendFunction = BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA;
      m_overlayMaterial->Init();

      RenderState state;
      state.depthTestEnabled  = false;
      state.depthWriteEnabled = false;
      state.blendOverride     = true;
      state.blendOverrideFunc = BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA;

      renderer->SetFramebuffer(viewport->m_framebuffer, GraphicBitFields::None);
      renderer->SetPassState(state);
      renderer->DrawFullQuad(m_overlayMaterial);
    }
    void EditorRenderer::Render(Renderer* renderer)
    {
      PreRender(renderer);
      SetLitMode(renderer, m_params.LitMode);

      m_passArray.clear();

      SceneRenderPathPtr sceneRenderer = m_sceneRenderPath;
      FramebufferPtr mainBuffer        = m_params.Viewport->m_framebuffer;
      auto copySceneToViewportBuffer   = [renderer, mainBuffer, sceneRenderer]()
      {
        // The scene path may leave the picture somewhere other than the viewport's buffer: screen space
        // reflections composite into a target of their own and the passes behind them follow it. The
        // editor continues drawing over that picture, so the buffer it draws into is filled from the
        // path's output rather than from a target the path may not have written.
        //
        // Resolved frame buffer contains correct scene rendering with post processings.
        // To continue rendering for editor objects, the scene is copied onto the main buffer.
        using Attachment = Framebuffer::Attachment;
        FramebufferPtr sceneOutput = sceneRenderer->GetOutputFramebuffer();
        if (sceneOutput == nullptr || sceneOutput == mainBuffer)
        {
          return;
        }

        RenderTargetPtr sceneColor = sceneOutput->GetColorAttachment(Attachment::ColorAttachment0);
        RenderTargetPtr mainColor  = mainBuffer->GetColorAttachment(Attachment::ColorAttachment0);
        if (sceneColor != nullptr && mainColor != nullptr)
        {
          renderer->CopyTexture(sceneColor, mainColor);
        }
      };

      if (GetRenderSystem()->IsSkipFrame())
      {
        sceneRenderer->Render(renderer);

        m_passArray.push_back(m_skipFramePass);
        RenderPath::Render(renderer);
      }
      else if (m_params.LitMode == EditorLitMode::Game)
      {
        m_params.App->HideGizmos();
        sceneRenderer->m_params.grid                             = nullptr;
        sceneRenderer->m_forwardRenderPass->m_params.onPreRender = nullptr;
        sceneRenderer->Render(renderer);

        // The scene the path produced may need to be drawn into the buffer the viewport draws into.
        copySceneToViewportBuffer();

        if (!m_uiRenderData.jobs.empty())
        {
          m_passArray.push_back(m_uiPass);
        }

        RenderPath::Render(renderer);
        m_params.App->ShowGizmos();
      }
      else
      {
        sceneRenderer->Render(renderer);

        // Draw scene and apply bloom effect.
        RenderPath::Render(renderer);
        m_passArray.clear();

        SetLitMode(renderer, EditorLitMode::EditorLit);

        copySceneToViewportBuffer();

        // Draw outlines.
        OutlineSelecteds(renderer);
        m_passArray.clear();

        // Draw editor objects.
        if (!m_probeVisual->m_params.Samples.empty())
        {
          m_probeVisual->m_params.Cam         = m_camera;
          m_probeVisual->m_params.FrameBuffer = mainBuffer;
          m_passArray.push_back(m_probeVisual);
        }

        if (!m_renderData.jobs.empty())
        {
          m_passArray.push_back(m_editorPass);
        }

        // Gizmos and editor billboards go to an overlay target of their own. They clear depth to stay in
        // front of the scene, and the scene's depth is what the occlusion grid is reduced from at the top of
        // the next frame, so clearing it here is what used to leave the editor with a grid it could not use.
        const bool hasOverlay =
            !m_gizmoPass->m_params.GizmoArray.empty() || !m_billboardPass->m_params.Billboards.empty();

        if (hasOverlay)
        {
          PrepareOverlay(renderer, m_params.Viewport);

          m_gizmoPass->m_params.FrameBuffer     = m_overlayFramebuffer;
          m_billboardPass->m_params.FrameBuffer = m_overlayFramebuffer;

          if (!m_gizmoPass->m_params.GizmoArray.empty())
          {
            // Clears depth buffer to draw remaining entities always on top.
            m_passArray.push_back(m_gizmoPass);
          }

          if (!m_billboardPass->m_params.Billboards.empty())
          {
            // Scene meshes can't block editor billboards. Desired for this case.
            m_passArray.push_back(m_billboardPass);
          }
        }

        RenderPath::Render(renderer);

        if (hasOverlay)
        {
          CompositeOverlay(renderer, m_params.Viewport);
        }

      }

      // Finally resolve the multi sampled main buffer if needed.
      if (mainBuffer->IsMultiSampled())
      {
        using Attachment = Framebuffer::Attachment;
        if (PrepareViewportResolveTarget(mainBuffer->GetColorAttachment(Attachment::ColorAttachment0)))
        {
          renderer->ResolveFramebuffer(mainBuffer, m_resolvedFramebuffer, {0});
        }
      }

      // We need to fill the alpha channel with one, imgui insist on blending alpha with window background.
      {
        RenderTargetPtr colorBuffer = mainBuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
        if (colorBuffer->IsMultiSampled())
        {
          colorBuffer = m_resolvedFramebuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
        }
        RenderTargetPtr alphaBuffer = MakeNewPtr<RenderTarget>();
        alphaBuffer->ReconstructIfNeeded(colorBuffer->m_width, colorBuffer->m_height, &colorBuffer->Settings());
        renderer->CopyTexture(colorBuffer, alphaBuffer);
        renderer->CopyTexture(alphaBuffer, colorBuffer, true);
      }

      PostRender(renderer);
    }

    void EditorRenderer::PreRender(Renderer* renderer)
    {
      App* app                            = m_params.App;
      m_camera                            = m_params.Viewport->GetCamera();

      // Override lit mode for simulation.
      if (app->m_gameMod == GameMod::Playing)
      {
        if (m_params.Viewport == app->GetSimulationViewport().get())
        {
          m_params.LitMode = EditorLitMode::Game;
        }
      }

      const PostProcessingSettingsPtr pps = GetEngineSettings().m_postProcessing;

      // Adjust scene lights.
      m_lightSystem->m_parentNode->OrphanSelf();
      m_camera->m_node->AddChild(m_lightSystem->m_parentNode);

      EditorScenePtr scene                            = app->GetCurrentScene();
      EditorViewport* viewport                        = static_cast<EditorViewport*>(m_params.Viewport);

      // Scene renderer will render the given scene independent of editor.
      // Editor objects will be drawn on top of scene.

      // Scene pass.
      m_sceneRenderPath->m_params.postProcessSettings = pps;
      m_sceneRenderPath->m_params.Cam                 = m_camera;
      // Keep depth for subsequent editor passes.
      m_sceneRenderPath->m_forwardRenderPass->m_params.invalidateDepthBuffer = false;

      m_sceneRenderPath->m_params.overrideLights.clear();
      if (m_params.LitMode == EditorLitMode::EditorLit)
      {
        m_sceneRenderPath->m_params.overrideLights = m_lightSystem->m_lights;
      }

      m_sceneRenderPath->m_params.MainFramebuffer = viewport->m_framebuffer;
      m_sceneRenderPath->m_params.Scene           = scene;

      if (!GetRenderSystem()->m_backbufferFormatIsSRGB)
      {
        // If a linear space back buffer is used, we need to apply gamma after imgui, so skip it in scene render path.
        m_sceneRenderPath->m_params.postProcessSettings->SetGammaCorrectionEnabledVal(false);
      }

      if (app->m_showSceneBoundary)
      {
        app->m_perFrameDebugObjects.push_back(CreateBoundingBoxDebugObject(scene->GetSceneBoundary()));
      }

      if (app->m_showBVHNodes)
      {
        scene->m_aabbTree.GetDebugBoundingBoxes(app->m_perFrameDebugObjects);
      }

      if (app->m_showPickingDebug)
      {
        if (app->m_dbgArrow)
        {
          app->m_perFrameDebugObjects.push_back(app->m_dbgArrow);
        }

        if (app->m_dbgFrustum)
        {
          app->m_perFrameDebugObjects.push_back(app->m_dbgFrustum);
        }
      }

      // Generate Selection boundary and Environment component boundary.
      m_selecteds.clear();
      scene->GetSelectedEntities(m_selecteds);

      // Construct gizmo objects.
      for (EntityPtr ntt : m_selecteds)
      {
        EnvironmentComponentPtr envCom = ntt->GetComponent<EnvironmentComponent>();

        if (envCom != nullptr && !ntt->IsA<Sky>())
        {
          // Build local-space BB from PositionOffset +/- Size*0.5.
          Vec3 offset = envCom->GetPositionOffsetVal();
          Vec3 half   = envCom->GetSizeVal() * 0.5f;
          BoundingBox localBB;
          localBB.min         = offset - half;
          localBB.max         = offset + half;

          // Use entity's world transform so the box is oriented.
          Mat4 worldTransform = ntt->m_node->GetTransform(TransformationSpace::TS_WORLD);
          app->m_perFrameDebugObjects.push_back(
              CreateBoundingBoxDebugObject(localBB, g_environmentGizmoColor, 1.0f, &worldTransform));
        }

        if (LightProbeVolumeComponentPtr probeCom = ntt->GetComponent<LightProbeVolumeComponent>())
        {
          // Same shape of box as an environment volume, in the owner's local space, so the volume
          // the box edit tool scales is exactly the volume that is drawn.
          Vec3 offset = probeCom->GetPositionOffsetVal();
          Vec3 half   = probeCom->GetSizeVal() * 0.5f;
          BoundingBox localBB;
          localBB.min         = offset - half;
          localBB.max         = offset + half;

          Mat4 worldTransform = ntt->m_node->GetTransform(TransformationSpace::TS_WORLD);
          app->m_perFrameDebugObjects.push_back(
              CreateBoundingBoxDebugObject(localBB, g_lightProbeGizmoColor, 1.0f, &worldTransform));
        }

        if (app->m_showSelectionBoundary && ntt->IsDrawable())
        {
          app->m_perFrameDebugObjects.push_back(CreateBoundingBoxDebugObject(ntt->GetBoundingBox(true)));
        }

        if (app->m_showDirectionalLightShadowFrustum)
        {
          // Directional light shadow map frustum
          if (ntt->IsA<DirectionalLight>())
          {
            EditorDirectionalLight* light = static_cast<EditorDirectionalLight*>(ntt.get());
            if (light->GetCastShadowVal())
            {
              app->m_perFrameDebugObjects.push_back(light->GetDebugShadowFrustum());
              app->m_perFrameDebugObjects.push_back(
                  CreateDebugFrustum(app->GetViewport(g_3dViewport)->GetCamera(), Vec3(0.6f, 0.2f, 0.8f), 1.5f));
            }
          }
        }
      }

      // Per frame objects.
      EntityPtrArray editorEntities;
      editorEntities.insert(editorEntities.end(),
                            app->m_perFrameDebugObjects.begin(),
                            app->m_perFrameDebugObjects.end());

      // Billboard pass.
      m_billboardPass->m_params.Billboards = scene->GetBillboards();
      m_billboardPass->m_params.Billboards.push_back(app->m_origin);
      m_billboardPass->m_params.Billboards.push_back(app->m_cursor);
      m_billboardPass->m_params.Viewport   = m_params.Viewport;

      // Grid. UpdateShaderParams (which Map()s GridPassData into slot 7) must happen AFTER the
      // shadow/ssao/sky passes that share slot 7 - earlier passes (e.g. ShadowPass via gauss blur)
      // would otherwise displace the grid buffer before the forward pass draws the grid. Hook it
      // into ForwardRenderPass::PreRender so the timing is right regardless of which earlier
      // passes are active in this frame.
      GridPtr grid                         = m_params.Viewport->IsA<EditorViewport2d>() ? app->m_2dGrid : app->m_grid;
      m_sceneRenderPath->m_params.grid     = grid;
      ForwardRenderPass* forwardRenderPass = m_sceneRenderPath->m_forwardRenderPass.get();
      forwardRenderPass->m_params.onPreRender = [grid, forwardRenderPass]()
      {
        grid->UpdateShaderParams();
        // Stage slot 7 on the forward render pass so ApplyRequirements (called at the top
        // of Render) re-binds the descriptor set to the grid's VkBuffer before the grid's
        // draw records. Earlier passes sharing slot 7 may have left the descriptor
        // pointing elsewhere; this gets it back on track.
        forwardRenderPass->GetRequirements().customUbos[7] = &grid->GetDataBuffer().GetBuffer();
      };

      // Light gizmos.
      for (Light* light : scene->GetLights())
      {
        if (light->GetLightType() == Light::LightType::Directional)
        {
          if (light->As<EditorDirectionalLight>()->GizmoActive())
          {
            editorEntities.push_back(light->Self<Entity>());
          }
        }
        else if (light->GetLightType() == Light::LightType::Spot)
        {
          if (light->As<EditorSpotLight>()->GizmoActive())
          {
            editorEntities.push_back(light->Self<Entity>());
          }
        }
        else
        {
          assert(light->GetLightType() == Light::LightType::Point);
          if (light->As<EditorPointLight>()->GizmoActive())
          {
            editorEntities.push_back(light->Self<Entity>());
          }
        }
      }

      // Add canvas gizmos.
      if (scene->IsLayerScene())
      {
        const EntityPtrArray& ntties = scene->GetEntities();
        for (EntityPtr ntt : ntties)
        {
          if (EditorCanvas* canvas = ntt->As<EditorCanvas>())
          {
            editorEntities.push_back(canvas->GetBorderGizmo());
          }
        }
      }

      // Reflection probe previews. A chrome sphere on the probe's own origin, showing the cubemap
      // the probe captured. It is editor only: the sphere is cached on this renderer and added to
      // this frame's editor entities, so it is never part of the scene and a game never sees it. It
      // is only drawn for the selected probe - a level full of probes would otherwise put a ball in
      // every room - and ShowChromeSphere turns it off for the selected one.
      std::unordered_map<ObjectId, EnvironmentComponent*> previewEnvironments;
      std::vector<ObjectId> sceneProbes;
      for (EntityPtr ntt : scene->GetEntities())
      {
        ReflectionProbe* probe = ntt->As<ReflectionProbe>();
        if (probe == nullptr)
        {
          continue;
        }

        EnvironmentComponentPtr env = probe->GetEnvironmentComponent();
        if (env == nullptr)
        {
          continue;
        }

        sceneProbes.push_back(probe->GetIdVal());

        // Only the probe being edited gets a ball: a level full of probes would otherwise put one
        // in every room, and the ball doubles as the selection feedback.
        if (std::find(m_selecteds.begin(), m_selecteds.end(), ntt) == m_selecteds.end())
        {
          continue;
        }

        SpherePtr& preview = m_reflectionPreviews[probe->GetIdVal()];
        if (preview == nullptr)
        {
          preview = MakeNewPtr<Sphere>();
          preview->SetNameVal("ReflectionProbePreview");

          // A reflection only material: the cubemap and nothing else, so the ball shows what the
          // probe captured instead of the room's own lighting.
          if (m_reflectionPreviewMaterial == nullptr)
          {
            m_reflectionPreviewMaterial = MakeNewPtr<Material>();
            m_reflectionPreviewMaterial->SetVertexShaderVal(
                GetShaderManager()->Create<Shader>(ShaderPath("defaultVertex.shader", true)));
            m_reflectionPreviewMaterial->SetFragmentShaderVal(
                GetShaderManager()->Create<Shader>(ShaderPath("reflectionPreviewFrag.shader", true)));
            m_reflectionPreviewMaterial->Init();
          }

          if (MaterialComponentPtr matComp = preview->GetMaterialComponent())
          {
            matComp->SetFirstMaterial(m_reflectionPreviewMaterial);
            matComp->Init(false);
          }

          if (MeshComponentPtr meshComp = preview->GetMeshComponent())
          {
            meshComp->Init(false);
          }
        }

        // The probe's own origin in world space, which is where the capture is rendered from and
        // what its parallax corrected reflections are projected against. PositionOffset moves the
        // box around that point, it does not move the point. The sphere primitive has a unit
        // radius, so a 0.25 scale is the half metre ball a probe is inspected with.
        preview->m_node->SetTranslation(probe->m_node->GetTranslation(TransformationSpace::TS_WORLD),
                                        TransformationSpace::TS_WORLD);
        preview->m_node->SetScale(Vec3(0.25f));

        previewEnvironments[preview->GetIdVal()] = env.get();
        editorEntities.push_back(preview);
      }

      // Drop the previews of probes that left the scene. A deselected probe keeps its cached
      // sphere, so selecting it again does not rebuild the mesh and material.
      for (auto it = m_reflectionPreviews.begin(); it != m_reflectionPreviews.end();)
      {
        if (std::find(sceneProbes.begin(), sceneProbes.end(), it->first) == sceneProbes.end())
        {
          it = m_reflectionPreviews.erase(it);
        }
        else
        {
          ++it;
        }
      }

      // Light probe sample points of the selected volumes, drawn by their own instanced pass.
      GatherLightProbeSamples(scene);

      // Editor pass.
      m_renderData.jobs.clear();
      EntityRawPtrArray rawNtties = ToEntityRawPtrArray(editorEntities);
      RenderJobProcessor::CreateRenderJobs(m_renderData.jobs, rawNtties);

      // A preview reflects the probe it belongs to: its jobs carry that volume's cubemap.
      for (RenderJob& job : m_renderData.jobs)
      {
        if (job.Entity == nullptr)
        {
          continue;
        }

        const auto envIt = previewEnvironments.find(job.Entity->GetIdVal());
        if (envIt != previewEnvironments.end())
        {
          job.EnvironmentVolume          = envIt->second;
          job.SecondaryEnvironmentVolume = nullptr;
        }
      }

      RenderJobProcessor::SeperateRenderData(m_renderData, true);

      m_editorPass->m_params.renderData     = &m_renderData;
      m_editorPass->m_params.Cam            = m_camera;
      m_editorPass->m_params.FrameBuffer    = viewport->m_framebuffer;
      m_editorPass->m_params.clearBuffer    = GraphicBitFields::None;

      // Skip frame pass.
      m_skipFramePass->m_params.frameBuffer = viewport->m_framebuffer;
      m_skipFramePass->m_material           = m_blackMaterial;

      // UI pass.
      UILayerPtrArray layers;
      m_uiRenderData.jobs.clear();
      GetUIManager()->GetLayers(viewport->m_viewportId, layers);

      for (const UILayerPtr& layer : layers)
      {
        EntityRawPtrArray rawNtties = ToEntityRawPtrArray(layer->m_scene->GetEntities());
        RenderJobProcessor::CreateRenderJobs(m_uiRenderData.jobs, rawNtties);
      }

      RenderJobProcessor::SeperateRenderData(m_uiRenderData, true);

      m_uiPass->m_params.renderData  = &m_uiRenderData;
      m_uiPass->m_params.Cam         = GetUIManager()->GetUICamera();
      m_uiPass->m_params.FrameBuffer = viewport->m_framebuffer;
      m_uiPass->m_params.clearBuffer = GraphicBitFields::DepthBits;

      // Gizmo Pass.
      m_gizmoPass->m_params.Viewport = viewport;

      EditorBillboardPtr anchorGizmo = nullptr;
      if (viewport->IsA<EditorViewport2d>())
      {
        anchorGizmo = app->m_anchor;
      }
      m_gizmoPass->m_params.GizmoArray = {app->m_gizmo, anchorGizmo};
    }

    void EditorRenderer::PostRender(Renderer* renderer) 
    { 
      renderer->FinishPass();
      m_params.App->m_perFrameDebugObjects.clear(); 
    }

    namespace
    {
      /** Colour a volume that has not been baked yet is drawn with. */
      const Vec3 UnbakedProbeColor(0.5f);

      /**
       * Colour of a baked probe: the DC term of its spherical harmonics.
       *
       * The coefficients are stored already convolved with the clamped cosine and divided by pi, so
       * the first one is the omnidirectional irradiance at that point. 0.282095 is the Y00 basis
       * value, the constant probeGI.shader applies for the same term, and the volume's intensity
       * scales it the way the forward pass scales the ambient it contributes.
       */
      Vec3 ProbeSampleColor(const ProbeVolumeData& data, size_t index, float intensity)
      {
        const size_t stride = (size_t) data.CoefficientCount() * 3;
        if ((index + 1) * stride > data.coefficients.size())
        {
          return Vec3(0.0f);
        }

        const float* coefficient = data.coefficients.data() + index * stride;
        const Vec3 color =
            Vec3(coefficient[0], coefficient[1], coefficient[2]) * (0.282095f * intensity);

        return glm::clamp(color, Vec3(0.0f), Vec3(1.0f));
      }
    } // namespace

    void EditorRenderer::GatherLightProbeSamples(const ScenePtr& scene)
    {
      m_probeVisual->m_params.Samples.clear();

      if (scene == nullptr)
      {
        return;
      }

      for (const EntityPtr& ntt : scene->GetEntities())
      {
        LightProbe* probe = ntt->As<LightProbe>();
        if (probe == nullptr)
        {
          continue;
        }

        // The volume's own toggle, not the selection: a level keeps the points on for the volume it
        // is lighting, and several volumes can be shown at once. The flag lives on the editor
        // component, the half a viewport view belongs to.
        LightProbeVolumeComponentPtr volume = probe->GetProbeVolume();
        if (volume == nullptr)
        {
          continue;
        }

        const auto* editorVolume = volume->As<EditorLightProbeVolumeComponent>();
        if (editorVolume == nullptr || !editorVolume->GetVisualiseProbeVal())
        {
          continue;
        }

        const ProbeVolumeData& data = volume->GetProbeData();
        const bool baked            = volume->IsBaked();

        // A baked volume keeps the resolution it was baked at while the box follows the entity,
        // which is the pairing the forward pass interpolates. Before a bake the grid is the one the
        // volume would bake, so the point layout can be inspected ahead of it. A grid the capacity
        // refuses is not drawn: it cannot be baked, and a preview of millions of points that will
        // never exist is worse than none.
        const UVec3 resolution  = baked ? data.resolution : volume->ComputeBakeResolution();
        const size_t probeCount = (size_t) resolution.x * resolution.y * resolution.z;
        if (probeCount == 0 || !volume->ProbeGridFits(resolution))
        {
          continue;
        }

        const Mat4 world      = probe->m_node->GetTransform(TransformationSpace::TS_WORLD);
        const Vec3 size       = volume->GetSizeVal();
        const Vec3 boxMin     = volume->GetPositionOffsetVal() - size * 0.5f;
        const float intensity = volume->GetIntensityVal();
        const float markerRadius = editorVolume->GetVisualiseProbeSizeVal();

        std::vector<Vec4>& samples = m_probeVisual->m_params.Samples;
        samples.reserve(samples.size() + probeCount * 2);

        for (uint z = 0; z < resolution.z; ++z)
        {
          for (uint y = 0; y < resolution.y; ++y)
          {
            for (uint x = 0; x < resolution.x; ++x)
            {
              const size_t index = ((size_t) z * resolution.y + y) * resolution.x + x;

              // The mask is the record of what the bake traced, so a zero here is a probe that
              // either sits inside geometry or was too far from a surface to be worth sampling -
              // and it carries a copy of a neighbour, which is not a value of its own to show. On
              // a sparse bake this is what turns the grid into the shape of the geometry: the
              // dots that appear are the probes that were actually traced.
              if (baked && index < data.valid.size() && data.valid[index] == 0)
              {
                continue;
              }

              // The same cell centres the baker traces, so a point sits where its probe is.
              const Vec3 uvw((x + 0.5f) / (float) resolution.x,
                             (y + 0.5f) / (float) resolution.y,
                             (z + 0.5f) / (float) resolution.z);
              const Vec3 local    = boxMin + uvw * size;
              const Vec3 position = Vec3(world * Vec4(local, 1.0f));
              const Vec3 color    = baked ? ProbeSampleColor(data, index, intensity) : UnbakedProbeColor;

              samples.push_back(Vec4(position, markerRadius));
              samples.push_back(Vec4(color, 1.0f));
            }
          }
        }
      }
    }

    void EditorRenderer::SetLitMode(Renderer* renderer, EditorLitMode mode)
    {
      switch (mode)
      {
        case EditorLitMode::EditorLit:
        case EditorLitMode::FullyLit:
        case EditorLitMode::Game:
          renderer->m_shadingMode = ShadingMode::None;
          break;
        case EditorLitMode::Lighing:
          renderer->m_shadingMode = ShadingMode::Lighting;
          break;
        case EditorLitMode::Albedo:
          renderer->m_shadingMode = ShadingMode::Albedo;
          break;
        case EditorLitMode::Normal:
          renderer->m_shadingMode = ShadingMode::Normal;
          break;
        case EditorLitMode::Metallic:
          renderer->m_shadingMode = ShadingMode::Metallic;
          break;
        case EditorLitMode::Roughness:
          renderer->m_shadingMode = ShadingMode::Roughness;
          break;
      }
    }

    void EditorRenderer::InitRenderer()
    {
      m_lightSystem   = MakeNewPtr<ThreePointLightSystem>();

      // Create render mode materials.
      m_unlitOverride = GetMaterialManager()->GetCopyOfUnlitMaterial();
      m_blackMaterial = GetMaterialManager()->GetCopyOfUnlitMaterial();
      m_unlitOverride->Init();
      m_blackMaterial->Init();

      m_billboardPass       = MakeNewPtr<BillboardPass>();
      m_sceneRenderPath     = MakeNewPtr<ForwardSceneRenderPath>();
      m_uiPass              = MakeNewPtr<ForwardRenderPass>();
      m_editorPass          = MakeNewPtr<ForwardRenderPass>();
      m_gizmoPass           = MakeNewPtr<GizmoPass>();
      m_outlinePass         = MakeNewPtr<OutlinePass>();
      m_probeVisual         = MakeNewPtr<LightProbeVisualPass>();
      m_skipFramePass       = MakeNewPtr<FullQuadPass>();
      m_resolvedFramebuffer = MakeNewPtr<Framebuffer>();
    }

    RenderTargetPtr EditorRenderer::PrepareViewportResolveTarget(RenderTargetPtr viewportColor)
    {
      using Attachment = Framebuffer::Attachment;

      if (viewportColor == nullptr || !viewportColor->IsMultiSampled())
      {
        return nullptr;
      }

      const int width  = viewportColor->m_width;
      const int height = viewportColor->m_height;

      // The frame the viewport just built, at the size the resolve has to write. Nothing here may replace the
      // color attachment once it exists: the handle ImGui already recorded names it, so a reconstruction
      // between the two would leave that handle pointing at a texture nothing writes.
      m_resolvedFramebuffer->ReconstructIfNeeded({width, height, false, true, MsaaSampleCount::x0});

      RenderTargetPtr resolvedRt = m_resolvedFramebuffer->GetColorAttachment(Attachment::ColorAttachment0);
      if (resolvedRt == nullptr)
      {
        // Same target the resolve builds for itself when it finds none: the viewport's format without the
        // samples. Built here, in the frame the image is recorded, it can be sampled above.
        TextureSettings rtSettings = viewportColor->Settings();
        rtSettings.msaaCount       = MsaaSampleCount::x0;

        resolvedRt                 = MakeNewPtr<RenderTarget>();
        resolvedRt->ReconstructIfNeeded(width, height, &rtSettings);

        m_resolvedFramebuffer->SetColorAttachment(Attachment::ColorAttachment0, resolvedRt);
      }

      viewportColor->m_resolvedTexture = resolvedRt;
      return resolvedRt;
    }

    void EditorRenderer::OutlineSelecteds(Renderer* renderer)
    {
      if (m_selecteds.empty())
      {
        return;
      }
      EntityPtrArray selecteds = m_selecteds; // Copy

      Viewport* viewport       = m_params.Viewport;
      CameraPtr viewportCamera = viewport->GetCamera();
      auto RenderFn            = [this, viewport, renderer, &viewportCamera](const EntityPtrArray& selection,
                                                                             const Vec4& color) -> void
      {
        if (selection.empty())
        {
          return;
        }

        EntityPtrArray highlightList = selection;

        // Extend the render jobs for prefabs.
        for (EntityPtr ntt : selection)
        {
          if (ntt->IsA<Prefab>())
          {
            auto addToSelectionFn = [&highlightList](Node* node) { highlightList.push_back(node->OwnerEntity()); };
            TraverseNodeHierarchyBottomUp(ntt->m_node, addToSelectionFn);
          }
        }

        RenderJobArray renderJobs;
        EntityRawPtrArray billboardNtties;
        for (EntityPtr entity : highlightList)
        {
          // Disable light gizmos
          if (Light* light = entity->As<Light>())
          {
            EnableLightGizmo(light, false);
          }

          // Add billboards to draw list
          EditorScenePtr scene = m_params.App->GetCurrentScene();
          EntityPtr billboard  = scene->GetBillboard(entity);

          if (billboard == nullptr)
          {
            // The map is filled lazily by the inspector, so a bulk selection holds entities
            // that never had a billboard made for them and their gizmo would never be
            // highlighted.
            scene->ValidateBillboard(entity);
            billboard = scene->GetBillboard(entity);
          }

          if (billboard)
          {
            static_cast<Billboard*>(billboard.get())->LookAt(viewportCamera, viewport->GetBillboardScale());
            billboardNtties.push_back(billboard.get());
          }
        }

        EntityRawPtrArray rawNtties = ToEntityRawPtrArray(highlightList);
        RenderJobProcessor::CreateRenderJobs(renderJobs, rawNtties, true);

        // One call for the whole batch: CreateRenderJobs reuses its output array when the size
        // matches, so calling it once per billboard overwrites the previous job and only the
        // last billboard of a bulk selection ends up highlighted.
        RenderJobArray billboardJobs;
        RenderJobProcessor::CreateRenderJobs(billboardJobs, billboardNtties, true);

        FrustumCull(renderJobs, viewportCamera, m_unCulledRenderJobs);

        // Billboards are appended after the cull, not before it: they are camera facing gizmos
        // that the editor draws without a frustum test everywhere else, and a gizmo bounding box
        // taken before LookAt sits at the origin, so culling them drops the highlight of every
        // selected light.
        m_unCulledRenderJobs.insert(m_unCulledRenderJobs.end(), billboardJobs.begin(), billboardJobs.end());

        // Set parameters of pass
        m_outlinePass->m_params.Camera       = viewportCamera;
        m_outlinePass->m_params.FrameBuffer  = viewport->m_framebuffer;
        m_outlinePass->m_params.OutlineColor = color;
        m_outlinePass->m_params.RenderJobs   = &m_unCulledRenderJobs;

        m_passArray.clear();
        m_passArray.push_back(m_outlinePass);
        RenderPath::Render(renderer);

        // Enable light gizmos back
        for (EntityPtr entity : highlightList)
        {
          if (Light* light = entity->As<Light>())
          {
            EnableLightGizmo(light, true);
          }
        }
      };

      EntityPtr primary = selecteds.back();

      selecteds.pop_back();
      RenderFn(selecteds, g_selectHighLightSecondaryColor);

      selecteds.clear();
      selecteds.push_back(primary);

      RenderFn(selecteds, g_selectHighLightPrimaryColor);
    }

  } // namespace Editor
} // namespace ToolKit
