/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Viewport.h"

#include "Camera.h"
#include "DirectionComponent.h"
#include "Node.h"
#include "Primative.h"
#include "Renderer.h"
#include "Scene.h"
#include "ToolKit.h"
#include "Util.h"

#include "DebugNew.h"

namespace ToolKit
{

  CameraPtr ViewportBase::GetCamera() const
  {
    if (m_attachedCamera != NullHandle)
    {
      if (ScenePtr currScene = GetSceneManager()->GetCurrentScene())
      {
        if (EntityPtr camNtt = currScene->GetEntity(m_attachedCamera))
        {
          if (CameraPtr cam = Cast<Camera>(camNtt))
          {
            assert(cam->IsA<Camera>());
            return cam;
          }
        }
      }
    }

    return m_camera;
  }

  void ViewportBase::SetCamera(CameraPtr cam)
  {
    m_camera         = cam;
    m_attachedCamera = NullHandle;
  }

  void ViewportBase::SwapCamera(CameraPtr& cam, ObjectId& attachment)
  {
    cam.swap(m_camera);

    ObjectId tmpHdnl = m_attachedCamera;
    m_attachedCamera = attachment;
    attachment       = tmpHdnl;
  }

  void ViewportBase::AttachCamera(ObjectId camId)
  {
    // Sanity checks
    bool isCamNull      = camId == NullHandle;
    bool isCamFromScene = GetSceneManager()->GetCurrentScene()->GetEntity(camId) != nullptr;
    assert(isCamNull || isCamFromScene && "Given camera must be in the current scene.");

    m_attachedCamera = camId;
  }

  void ViewportBase::AttachCamera(CameraPtr cam) { AttachCamera(cam->GetIdVal()); }

  ViewportBase::ViewportBase()
  {
    m_camera         = MakeNewPtr<Camera>();
    m_viewportId     = GetHandleManager()->GenerateHandle();
    m_attachedCamera = NullHandle;
  }

  ViewportBase::~ViewportBase() {}

  Viewport::Viewport() {}

  Viewport::Viewport(float width, float height) : m_wndContentAreaSize(width, height)
  {
    GetCamera()->SetLens(glm::quarter_pi<float>(), width / height);
    ReInitViewport();
  }

  Viewport::~Viewport() {}

  void Viewport::OnResizeContentArea(float width, float height)
  {
    m_wndContentAreaSize.x = width;
    m_wndContentAreaSize.y = height;
    ReInitViewport();
  }

  void Viewport::AdjustZoom(float delta)
  {
    CameraPtr cam = GetCamera();
    cam->m_node->Translate(Vec3(0.0f, 0.0f, -delta), TransformationSpace::TS_LOCAL);

    if (cam->IsOrtographic())
    {
      float zoom = cam->GetOrthographicScaleVal() - delta;
      cam->SetOrthographicScaleVal(glm::max(zoom, 0.01f));
    }
  }

  TextureSettings Viewport::GetRenderTargetSettings()
  {
    TextureSettings texureSet;
    if (!GetEngineSettings().m_graphics->GetHDRPipelineVal())
    {
      texureSet.InternalFormat = GraphicTypes::FormatRGBA8;
      texureSet.Type           = GraphicTypes::TypeUnsignedByte;
    }

    return texureSet;
  }

  void Viewport::ResetViewportImage(const TextureSettings& settings)
  {
    EngineSettings& engineSettings = GetEngineSettings();
    if (m_framebuffer == nullptr)
    {
      m_framebuffer = MakeNewPtr<Framebuffer>("ViewportFB");
    }

    float resScale = engineSettings.m_graphics->GetRenderResolutionScaleVal();
    int width      = (int) glm::round(m_wndContentAreaSize.x * resScale);
    int height     = (int) glm::round(m_wndContentAreaSize.y * resScale);

    int msaaVal    = engineSettings.m_graphics->GetMSAAVal().GetValue<int>();
    m_framebuffer->ReconstructIfNeeded({width, height, false, true, msaaVal});

    m_renderTarget = MakeNewPtr<RenderTarget>(width, height, settings);
    m_renderTarget->Init();
    m_framebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_renderTarget);
  }

  Ray Viewport::RayFromMousePosition()
  {
    Vec2 ssp = GetLastMousePosScreenSpace();
    return RayFromScreenSpacePoint(ssp);
  }

  Ray Viewport::RayFromScreenSpacePoint(const Vec2& pnt)
  {
    Vec2 mcInVs = TransformScreenToViewportSpace(pnt);

    Ray ray;
    ray.position  = TransformViewportToWorldSpace(mcInVs);

    CameraPtr cam = GetCamera();
    if (cam->IsOrtographic())
    {
      ray.direction = cam->GetComponent<DirectionComponent>()->GetDirection();
    }
    else
    {
      ray.direction = glm::normalize(ray.position - cam->m_node->GetTranslation(TransformationSpace::TS_WORLD));
    }

    return ray;
  }

  Vec3 Viewport::GetLastMousePosWorldSpace() { return TransformViewportToWorldSpace(GetLastMousePosViewportSpace()); }

  Vec2 Viewport::GetLastMousePosViewportSpace()
  {
    Vec2 screenPoint = m_lastMousePosRelContentArea;
    screenPoint.y    = m_wndContentAreaSize.y - screenPoint.y;

    return screenPoint;
  }

  Vec2 Viewport::GetLastMousePosScreenSpace()
  {
    Vec2 screenPoint = GetLastMousePosViewportSpace();
    screenPoint.y    = m_wndContentAreaSize.y - screenPoint.y;

    return m_contentAreaLocation + screenPoint;
  }

  Vec3 Viewport::TransformViewportToWorldSpace(const Vec2& pnt)
  {
    Vec3 pnt3d    = Vec3(pnt, 0.0f);

    CameraPtr cam = GetCamera();
    Mat4 view     = cam->GetViewMatrix();
    Mat4 project  = cam->GetProjectionMatrix();

    return glm::unProject(pnt3d, view, project, Vec4(0.0f, 0.0f, m_wndContentAreaSize.x, m_wndContentAreaSize.y));
  }

  Vec2 Viewport::TransformWorldSpaceToScreenSpace(const Vec3& pnt)
  {
    CameraPtr cam  = GetCamera();
    Mat4 view      = cam->GetViewMatrix();
    Mat4 project   = cam->GetProjectionMatrix();

    Vec3 screenPos = glm::project(pnt, view, project, Vec4(0.0f, 0.0f, m_wndContentAreaSize.x, m_wndContentAreaSize.y));

    screenPos.x += m_contentAreaLocation.x;
    screenPos.y  = m_wndContentAreaSize.y + m_contentAreaLocation.y - screenPos.y;

    return screenPos;
  }

  Vec2 Viewport::TransformScreenToViewportSpace(const Vec2& pnt)
  {
    Vec2 vp = pnt - m_contentAreaLocation;   // In window space.
    vp.y    = m_wndContentAreaSize.y - vp.y; // In viewport space.
    return vp;
  }

  bool Viewport::IsOrthographic()
  {
    if (CameraPtr cam = GetCamera())
    {
      return cam->IsOrtographic();
    }

    return false;
  }

  float Viewport::GetBillboardScale()
  {
    CameraPtr cam = GetCamera();
    if (cam->IsOrtographic())
    {
      return cam->GetOrthographicScaleVal();
    }

    return m_wndContentAreaSize.y;
  }

  void Viewport::ReInitViewport() { ResetViewportImage(GetRenderTargetSettings()); }

} // namespace ToolKit
