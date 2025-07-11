/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
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
#include <EnvironmentComponent.h>
#include <GradientSky.h>
#include <Material.h>
#include <MaterialComponent.h>
#include <MathUtil.h>
#include <Prefab.h>
#include <Primative.h>
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
      m_billboardPass        = nullptr;
      m_lightSystem          = nullptr;
      m_sceneRenderPath      = nullptr;
      m_uiPass               = nullptr;
      m_editorPass           = nullptr;
      m_gizmoPass            = nullptr;
      m_outlinePass          = nullptr;
      m_gammaTonemapFxaaPass = nullptr;
    }

    void EditorRenderer::Render(Renderer* renderer)
    {
      PreRender();
      SetLitMode(renderer, m_params.LitMode);

      m_passArray.clear();

      SceneRenderPathPtr sceneRenderer = m_sceneRenderPath;
      if (GetRenderSystem()->IsSkipFrame())
      {
        sceneRenderer->Render(renderer);

        m_passArray.push_back(m_skipFramePass);
        RenderPath::Render(renderer);

        PostRender();
        return;
      }

      switch (m_params.LitMode)
      {
      case EditorLitMode::Game:
        m_params.App->HideGizmos();
        sceneRenderer->m_params.grid = nullptr;
        sceneRenderer->Render(renderer);
        m_passArray.push_back(m_uiPass);
        if (m_gammaTonemapFxaaPass->IsEnabled())
        {
          m_passArray.push_back(m_gammaTonemapFxaaPass);
        }
        RenderPath::Render(renderer);
        m_params.App->ShowGizmos();
        break;
      default:
        sceneRenderer->Render(renderer);
        break;
      }

      if (m_params.LitMode != EditorLitMode::Game)
      {
        // Draw scene and apply bloom effect.
        RenderPath::Render(renderer);
        m_passArray.clear();

        SetLitMode(renderer, EditorLitMode::EditorLit);

        // Draw outlines.
        OutlineSelecteds(renderer);
        m_passArray.clear();

        // Draw editor objects.
        m_passArray.push_back(m_editorPass);

        // Clears depth buffer to draw remaining entities always on top.
        m_passArray.push_back(m_gizmoPass);

        // Scene meshes can't block editor billboards. Desired for this case.
        m_passArray.push_back(m_billboardPass);

        // Post process.
        if (m_gammaTonemapFxaaPass->IsEnabled())
        {
          m_passArray.push_back(m_gammaTonemapFxaaPass);
        }

        RenderPath::Render(renderer);
      }

      PostRender();
    }

    void EditorRenderer::PreRender()
    {
      App* app                            = m_params.App;
      m_camera                            = m_params.Viewport->GetCamera();

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

      m_sceneRenderPath->m_params.overrideLights.clear();
      if (m_params.LitMode == EditorLitMode::EditorLit)
      {
        m_sceneRenderPath->m_params.overrideLights = m_lightSystem->m_lights;
      }

      m_sceneRenderPath->m_params.MainFramebuffer = viewport->m_framebuffer;
      m_sceneRenderPath->m_params.Scene           = scene;

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
          app->m_perFrameDebugObjects.push_back(
              CreateBoundingBoxDebugObject(envCom->GetBoundingBox(), g_environmentGizmoColor, 1.0f));
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
      m_billboardPass->m_params.Viewport = m_params.Viewport;

      // Grid.
      GridPtr grid                       = m_params.Viewport->IsA<EditorViewport2d>() ? app->m_2dGrid : app->m_grid;
      grid->UpdateShaderParams();
      m_sceneRenderPath->m_params.grid = grid;

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

      // Editor pass.
      m_renderData.jobs.clear();
      EntityRawPtrArray rawNtties = ToEntityRawPtrArray(editorEntities);
      RenderJobProcessor::CreateRenderJobs(m_renderData.jobs, rawNtties);
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

      m_uiPass->m_params.renderData                          = &m_uiRenderData;
      m_uiPass->m_params.Cam                                 = GetUIManager()->GetUICamera();
      m_uiPass->m_params.FrameBuffer                         = viewport->m_framebuffer;
      m_uiPass->m_params.clearBuffer                         = GraphicBitFields::DepthBits;

      // Post process pass
      m_gammaTonemapFxaaPass->m_params.frameBuffer           = viewport->m_framebuffer;
      m_gammaTonemapFxaaPass->m_params.enableGammaCorrection = GetRenderSystem()->IsGammaCorrectionNeeded();
      m_gammaTonemapFxaaPass->m_params.enableFxaa            = pps->GetFXAAEnabledVal();
      m_gammaTonemapFxaaPass->m_params.enableTonemapping     = pps->GetTonemappingEnabledVal();
      m_gammaTonemapFxaaPass->m_params.gamma                 = pps->GetGammaVal();
      m_gammaTonemapFxaaPass->m_params.screenSize            = viewport->m_size;
      m_gammaTonemapFxaaPass->m_params.tonemapMethod         = pps->GetTonemapperModeVal().GetEnum<TonemapMethod>();

      // Gizmo Pass.
      m_gizmoPass->m_params.Viewport                         = viewport;

      EditorBillboardPtr anchorGizmo                         = nullptr;
      if (viewport->IsA<EditorViewport2d>())
      {
        anchorGizmo = app->m_anchor;
      }
      m_gizmoPass->m_params.GizmoArray = {app->m_gizmo, anchorGizmo};
    }

    void EditorRenderer::PostRender() { m_params.App->m_perFrameDebugObjects.clear(); }

    void EditorRenderer::SetLitMode(Renderer* renderer, EditorLitMode mode)
    {
      switch (mode)
      {
      case EditorLitMode::EditorLit:
      case EditorLitMode::FullyLit:
      case EditorLitMode::Game:
        renderer->m_renderOnlyLighting = false;
        break;
      case EditorLitMode::LightingOnly:
        renderer->m_renderOnlyLighting = true;
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

      m_billboardPass        = MakeNewPtr<BillboardPass>();
      m_sceneRenderPath      = MakeNewPtr<ForwardSceneRenderPath>();
      m_uiPass               = MakeNewPtr<ForwardRenderPass>();
      m_editorPass           = MakeNewPtr<ForwardRenderPass>();
      m_gizmoPass            = MakeNewPtr<GizmoPass>();
      m_outlinePass          = MakeNewPtr<OutlinePass>();
      m_skipFramePass        = MakeNewPtr<FullQuadPass>();
      m_gammaTonemapFxaaPass = MakeNewPtr<GammaTonemapFxaaPass>();
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
        RenderJobArray billboardJobs;
        for (EntityPtr entity : highlightList)
        {
          // Disable light gizmos
          if (Light* light = entity->As<Light>())
          {
            EnableLightGizmo(light, false);
          }

          // Add billboards to draw list
          EntityPtr billboard = m_params.App->GetCurrentScene()->GetBillboard(entity);

          if (billboard)
          {
            static_cast<Billboard*>(billboard.get())->LookAt(viewportCamera, viewport->GetBillboardScale());

            RenderJobProcessor::CreateRenderJobs(billboardJobs, billboard);
          }
        }

        EntityRawPtrArray rawNtties = ToEntityRawPtrArray(highlightList);
        RenderJobProcessor::CreateRenderJobs(renderJobs, rawNtties, true);
        renderJobs.insert(renderJobs.end(), billboardJobs.begin(), billboardJobs.end());

        FrustumCull(renderJobs, viewportCamera, m_unCulledRenderJobs);

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
