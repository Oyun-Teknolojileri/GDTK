/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "EditorLight.h"
#include "EditorTypes.h"
#include "Gizmo.h"
#include "GizmoPass.h"

#include <BillboardPass.h>
#include <BloomPass.h>
#include <ForwardSceneRenderPath.h>
#include <GammaTonemapFxaaPass.h>
#include <OutlinePass.h>
#include <Types.h>

namespace ToolKit
{
  namespace Editor
  {

    /** Enumeration for available render modes for the editor. */
    enum class EditorLitMode
    {
      /**
       * Uses editor lights which is a three point lighting system attached to
       * camera. Always lit where the viewport camera looks at.
       */
      EditorLit,
      /** Uses lights in the scene with all rendering features enabled. */
      FullyLit,
      /** Shows lighting result with a white material assigned to all objects. */
      Lighing,
      Albedo,
      Normal,
      Metallic,
      Roughness,
      /** Renders scene exactly as in game. */
      Game
    };

    struct TK_EDITOR_API EditorRenderParams
    {
      class App* App                 = nullptr;
      class EditorViewport* Viewport = nullptr;
      EditorLitMode LitMode          = EditorLitMode::EditorLit;
    };

    class TK_EDITOR_API EditorRenderer : public RenderPath
    {
     public:
      EditorRenderer();
      explicit EditorRenderer(const EditorRenderParams& params);
      virtual ~EditorRenderer();

      void Render(Renderer* renderer) override;
      void PreRender(Renderer* renderer) override;
      void PostRender(Renderer* renderer) override;

      /**
       * Builds the single sample target the viewport's frames are resolved into, sized to the viewport's
       * render target, and assigns it to that target as its resolved texture.
       *
       * The viewport calls this from the resize, while it is recording the frame's image - earlier than the
       * resolve that fills it, which is the point: the handle recorded there is what ImGui samples when the
       * frame is submitted, so the target it names has to exist and has to be the one this frame's resolve
       * writes. Idempotent, a matching target is left alone.
       * @param viewportColor The viewport's render target, a multisampled one to resolve.
       * @return The resolved target, or null when there is nothing to resolve.
       */
      RenderTargetPtr PrepareViewportResolveTarget(RenderTargetPtr viewportColor);

     private:
      void SetLitMode(Renderer* renderer, EditorLitMode mode);
      void InitRenderer();
      void OutlineSelecteds(Renderer* renderer);

     public:
      /** Pass parameters. */
      EditorRenderParams m_params;

     private:
      /** Three point lighting system which is used to illuminate the scene in EditorLit mode. */
      ThreePointLightSystemPtr m_lightSystem = nullptr;

      /** Override material for EditorLitMode::Unlit. */
      MaterialPtr m_unlitOverride            = nullptr;
      MaterialPtr m_blackMaterial            = nullptr;

      BillboardPassPtr m_billboardPass       = nullptr;
      SceneRenderPathPtr m_sceneRenderPath   = nullptr;
      ForwardRenderPassPtr m_uiPass          = nullptr;
      ForwardRenderPassPtr m_editorPass      = nullptr;
      GizmoPassPtr m_gizmoPass               = nullptr;
      SSAOPassPtr m_ssaoPass                 = nullptr;
      OutlinePassPtr m_outlinePass           = nullptr;
      FullQuadPassPtr m_skipFramePass        = nullptr;
      CameraPtr m_camera                     = nullptr;

      /** Selected entity list. */
      EntityPtrArray m_selecteds;

      RenderData m_renderData;
      RenderData m_uiRenderData;

      /** Internal render job array for rendering selected entities. */
      RenderJobArray m_unCulledRenderJobs;

      /**
       * The overlay target the gizmos and the editor billboards draw into, with a depth buffer of its own.
       *
       * They clear depth so they sit in front of the scene, and the scene's depth is what the occlusion grid
       * is reduced from at the top of the next frame. Clearing it left the grid describing a cleared buffer - which
       * silently turns culling off at best, and at worst has the depth mask sphere's near depths culling scene
       * objects away. Drawing them apart and compositing the color keeps the scene depth untouched.
       */
      FramebufferPtr m_overlayFramebuffer = nullptr;
      RenderTargetPtr m_overlayRt         = nullptr;
      MaterialPtr m_overlayMaterial       = nullptr;

      /** Sizes and clears the overlay target the gizmos and billboards draw into.
       *  @param renderer is the renderer to size the target with.
       *  @param viewport supplies the size.
       */
      void PrepareOverlay(Renderer* renderer, Viewport* viewport);

      /** Blends the overlay target over the viewport frame buffer, color only, no depth.
       *  @param renderer is the renderer to draw with.
       *  @param viewport is the viewport whose frame buffer receives the overlay.
       */
      void CompositeOverlay(Renderer* renderer, Viewport* viewport);

      /** Resolved viewport frame buffer is stored here. */
      FramebufferPtr m_resolvedFramebuffer = nullptr;
    };

  } // namespace Editor
} // namespace ToolKit
