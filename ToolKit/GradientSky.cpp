/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "GradientSky.h"

#include "Camera.h"
#include "EnvironmentComponent.h"
#include "Material.h"
#include "MathUtil.h"
#include "RHI.h"
#include "RenderSystem.h"
#include "Shader.h"
#include "Stats.h"
#include "TKOpenGL.h"

#include <DebugNew.h>

namespace ToolKit
{

  struct GradientDefaultParams
  {
    const Vec3 colors[3]  = {Vec3(0.3f, 0.3f, 1.0f), Vec3(1.0f, 1.0f, 0.8f), Vec3(0.5f, 0.3f, 0.1f)};
    const float exponent  = 0.3f;
    const String bakePath = ConcatPaths({"ToolKit", TKDefaultGradientSky});
  } GradientDefaults;

  TKDefineClass(GradientSky, SkyBase);

  GradientSky::GradientSky() {}

  GradientSky::~GradientSky() {}

  void GradientSky::Init()
  {
    if (m_initialized || m_waitingForInit)
    {
      return;
    }

    SkyBase::Init();

    // Skybox material
    ShaderPtr vert = GetShaderManager()->Create<Shader>(ShaderPath("skyboxVert.shader", true));
    ShaderPtr frag = GetShaderManager()->Create<Shader>(ShaderPath("gradientSkyboxFrag.shader", true));

    ConstructSkyMaterial(vert, frag);

    GradientSkyPtr self = Self<GradientSky>(); // Make sure, the object will stay valid.
    RenderTask task {[self, this](Renderer* renderer) -> void
                     {
                       if (!self || m_initialized)
                       {
                         return;
                       }

                       // Render gradient to cubemap and store the output
                       GenerateGradientCubemap(renderer);

                       // Create irradiance map from cubemap and set
                       if (HdriPtr hdri = GetHdri())
                       {
                         if (IsDefault() && GetIrradianceBakeFileVal().empty())
                         {
                           // Gradient has default values, we can use default gradient sky.
                           hdri->TrySettingCacheFiles(GradientDefaults.bakePath);
                         }

                         if (hdri->_diffuseBakeFile.empty())
                         {
                           hdri->GenerateIrradianceCaches(renderer);
                         }
                         else
                         {
                           hdri->LoadIrradianceCaches(renderer);
                         }
                       }

                       m_initialized    = true;
                       m_waitingForInit = false;
                     }};

    GetRenderSystem()->AddRenderTask(task);
    m_waitingForInit = true;
  }

  MaterialPtr GradientSky::GetSkyboxMaterial()
  {
    Init();

    m_skyboxMaterial->UpdateProgramUniform("topColor", GetTopColorVal());
    m_skyboxMaterial->UpdateProgramUniform("middleColor", GetMiddleColorVal());
    m_skyboxMaterial->UpdateProgramUniform("bottomColor", GetBottomColorVal());
    m_skyboxMaterial->UpdateProgramUniform("exponent", GetGradientExponentVal());

    return m_skyboxMaterial;
  }

  bool GradientSky::IsReadyToRender() { return m_skyboxMaterial != nullptr; }

  void GradientSky::ParameterConstructor()
  {
    Super::ParameterConstructor();

    TopColor_Define(GradientDefaults.colors[0], "Sky", 90, true, true, {true});
    MiddleColor_Define(GradientDefaults.colors[1], "Sky", 90, true, true, {true});
    BottomColor_Define(GradientDefaults.colors[2], "Sky", 90, true, true, {true});
    GradientExponent_Define(GradientDefaults.exponent, "Sky", 90, true, true, {false, true, 0.0f, 10.0f, 0.02f});

    // Update defaults.
    SetNameVal("Gradient Sky");
  }

  void GradientSky::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    GradientSkyWeakPtr self = Self<GradientSky>();
    ReGenerateIrradianceMap_Define(
        [self]() -> void
        {
          GetRenderSystem()->AddRenderTask({[self](Renderer* renderer) -> void
                                            {
                                              if (GradientSkyPtr gradientSky = self.lock())
                                              {
                                                gradientSky->GenerateGradientCubemap(renderer);
                                                if (HdriPtr hdr = gradientSky->GetHdri())
                                                {
                                                  hdr->GenerateIrradianceCaches(renderer);
                                                }
                                              }
                                            }});
        },
        SkyCategory.Name,
        SkyCategory.Priority,
        true,
        true);
  }

  void GradientSky::GenerateGradientCubemap(Renderer* renderer)
  {
    const TextureSettings set = {GraphicTypes::TargetCubeMap,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::SampleNearest,
                                 GraphicTypes::SampleNearest,
                                 GraphicTypes::FormatRGBA16F,
                                 GraphicTypes::FormatRGBA,
                                 GraphicTypes::TypeFloat,
                                 1,
                                 false};

    int size                  = m_skyMapSize;
    RenderTargetPtr cubemap   = MakeNewPtr<RenderTarget>(size, size, set);
    cubemap->Init();

    // Create material
    m_skyboxMaterial->UpdateProgramUniform("topColor", GetTopColorVal());
    m_skyboxMaterial->UpdateProgramUniform("middleColor", GetMiddleColorVal());
    m_skyboxMaterial->UpdateProgramUniform("bottomColor", GetBottomColorVal());
    m_skyboxMaterial->UpdateProgramUniform("exponent", GetGradientExponentVal());

    // Views for 6 different angles
    CameraPtr cam = MakeNewPtr<Camera>();
    cam->SetLens(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

    Mat4 views[]                  = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                                     glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                                     glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                                     glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                                     glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                                     glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};

    FramebufferPtr skyFrameBuffer = MakeNewPtr<Framebuffer>(FramebufferSettings {size, size, false, false}, "SkyFB");
    skyFrameBuffer->Init();

    for (int i = 0; i < 6; ++i)
    {
      Vec3 pos;
      Quaternion rot;
      Vec3 sca;
      DecomposeMatrix(views[i], &pos, &rot, &sca);

      cam->m_node->SetTranslation(ZERO, TransformationSpace::TS_WORLD);
      cam->m_node->SetOrientation(rot, TransformationSpace::TS_WORLD);
      cam->m_node->SetScale(sca);

      skyFrameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                         cubemap,
                                         0,
                                         -1,
                                         (Framebuffer::CubemapFace) i);

      renderer->SetFramebuffer(skyFrameBuffer, GraphicBitFields::None);
      renderer->DrawCube(cam, m_skyboxMaterial);
    }

    CubeMapPtr newCubemap = MakeNewPtr<CubeMap>();
    newCubemap->Consume(cubemap);

    HdriPtr self    = GetHdri();
    self->m_cubemap = newCubemap;
    self->m_width   = newCubemap->m_width;
    self->m_height  = newCubemap->m_height;
  }

  XmlNode* GradientSky::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

  bool GradientSky::IsDefault()
  {
    bool isDefault  = VecAllEqual(GetTopColorVal(), GradientDefaults.colors[0]);
    isDefault      &= VecAllEqual(GetMiddleColorVal(), GradientDefaults.colors[1]);
    isDefault      &= VecAllEqual(GetBottomColorVal(), GradientDefaults.colors[2]);
    isDefault      &= glm::equal(GetGradientExponentVal(), GradientDefaults.exponent);

    return isDefault;
  }

} // namespace ToolKit