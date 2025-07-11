/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Sky.h"

#include "DirectionComponent.h"
#include "EnvironmentComponent.h"
#include "FileManager.h"
#include "GradientSky.h"
#include "Image.h"
#include "Material.h"
#include "RenderSystem.h"
#include "Shader.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  // SkyBase
  //////////////////////////////////////////

  TKDefineClass(SkyBase, Entity);

  SkyBase::SkyBase() { m_partOfAABBTree = false; }

  void SkyBase::NativeConstruct() { Super::NativeConstruct(); }

  void SkyBase::Init()
  {
    if (m_initialized)
    {
      return;
    }

    // Create an environment component with hdr.
    EnvironmentComponentPtr envComp = GetComponent<EnvironmentComponent>();
    if (envComp == nullptr)
    {
      // Create a default environment component.
      envComp = AddComponent<EnvironmentComponent>();
    }

    HdriPtr hdri = nullptr;
    if (IsA<GradientSky>())
    {
      // Provide an empty hdr to construct gradient sky.
      hdri = MakeNewPtr<Hdri>();
    }
    else
    {
      // Check if there is a loaded hdr.
      hdri = envComp->GetHdriVal();
      if (hdri == nullptr)
      {
        // Use default hdr image.
        TextureManager* tman = GetTextureManager();
        hdri                 = tman->Create<Hdri>(tman->GetDefaultResource(Hdri::StaticClass()));
      }
    }

    // Check sky hdr caches.
    hdri->TrySettingCacheFiles(GetIrradianceBakeFileVal());

    // Associate hdr.
    envComp->SetHdriVal(hdri);

    envComp->SetSizeVal(Vec3(TK_FLT_MAX));
    envComp->OwnerEntity(Self<Entity>());
    envComp->Init(false);

    // Do not expose environment component
    envComp->m_localData.ExposeByCategory(false, EnvironmentComponentCategory);
  }

  void SkyBase::ReInit()
  {
    m_initialized = false;
    Init();
  }

  XmlNode* SkyBase::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlNode* nttNode = Super::DeSerializeImp(info, parent);
    return nttNode->first_node(StaticClass()->Name.c_str());
  }

  bool SkyBase::IsInitialized() { return m_initialized; }

  MaterialPtr SkyBase::GetSkyboxMaterial() { return m_skyboxMaterial; }

  HdriPtr SkyBase::GetHdri()
  {
    HdriPtr hdri = GetComponent<EnvironmentComponent>()->GetHdriVal();
    return hdri;
  }

  const BoundingBox& SkyBase::GetBoundingBox(bool inWorld) { return unitBox; }

  bool SkyBase::IsReadyToRender()
  {
    if (EnvironmentComponentPtr envComp = GetComponent<EnvironmentComponent>())
    {
      if (HdriPtr hdri = envComp->GetHdriVal())
      {
        return hdri->m_initiated;
      }
    }

    return false;
  }

  void SkyBase::ParameterConstructor()
  {
    Super::ParameterConstructor();

    DrawSky_Define(true, SkyCategory.Name, SkyCategory.Priority, true, true);
    Illuminate_Define(true, SkyCategory.Name, SkyCategory.Priority, true, true);
    Intensity_Define(1.0f, SkyCategory.Name, SkyCategory.Priority, true, true, {false, true, 0.0f, 100000.0f, 0.1f});

    auto createParameterVariantFn = [](const String& name, int val)
    {
      ParameterVariant param {val};
      param.m_name = name;
      return param;
    };

    SkyBaseWeakPtr self = Self<SkyBase>();
    BakeIrradianceMap_Define(
        [self]() -> void
        {
          GetRenderSystem()->AddRenderTask(
              {[self](Renderer* renderer) -> void
               {
                 if (!self.expired())
                 {
                   SkyBasePtr skyBase = self.lock();
                   if (HdriPtr hdr = skyBase->GetHdri())
                   {
                     auto bakeFn = [renderer, skyBase](CubeMapPtr cubemap, const String& file, int level) -> void
                     {
                       float* pixelBuffer;
                       float exposure = 1.0f;

                       renderer->GenerateEquiRectengularProjection(cubemap, level, exposure, (void**) &pixelBuffer);

                       UVec2 rectSize = cubemap->GetEquiRectengularMapSize();
                       int mipWidth   = rectSize.x >> level;
                       int mipHeight  = rectSize.y >> level;

                       WriteHdr(file, mipWidth, mipHeight, 4, pixelBuffer);
                       SafeDelArray(pixelBuffer);
                     };

                     // Create cache folder.
                     const static String cacheFolder = TexturePath(TKIrradianceCacheFolder);
                     if (!CheckFile(cacheFolder))
                     {
                       GetFileManager()->CreateResourceFolder(cacheFolder);
                     }

                     // Bake diffuse env map for level 0.
                     String baseName = hdr->GenerateBakedEnvironmentFileBaseName();
                     skyBase->SetIrradianceBakeFileVal(baseName);

                     String bakeFile = hdr->ToDiffuseIrradianceFileName(baseName) + HDR;
                     bakeFile        = TexturePath(bakeFile);
                     bakeFn(hdr->m_diffuseEnvMap, bakeFile, 0);

                     // Bake specular env map for all levels.
                     if (hdr->m_specularEnvMap)
                     {
                       int lodCount = glm::min(hdr->m_specularEnvMap->CalculateMipmapLevels(),
                                               (int) RHIConstants::SpecularIBLLods);

                       bakeFile     = hdr->ToSpecularIrradianceFileName(baseName);
                       for (int i = 1; i < lodCount; i++) // Skip first lod which is the original texture.
                       {
                         String file = bakeFile + std::to_string(i) + HDR;
                         file        = TexturePath(file);
                         bakeFn(hdr->m_specularEnvMap, file, i);
                       }
                     }

                     TK_LOG("Irradiance map baked.");
                   }
                 }
               },
               nullptr,
               RenderTaskPriority::Low});
        },
        SkyCategory.Name,
        SkyCategory.Priority,
        true,
        true);

    IrradianceBakeFile_Define("", SkyCategory.Name, SkyCategory.Priority, true, false);

    SetNameVal("SkyBase");
  }

  void SkyBase::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    ParamIlluminate().m_onValueChangedFn.clear();
    ParamIlluminate().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          if (IsInitialized())
          {
            GetComponent<EnvironmentComponent>()->SetIlluminateVal(std::get<bool>(newVal));
          }
        });

    ParamIntensity().m_onValueChangedFn.clear();
    ParamIntensity().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          if (IsInitialized())
          {
            GetComponent<EnvironmentComponent>()->SetIntensityVal(std::get<float>(newVal));
          }
        });
  }

  void SkyBase::ConstructSkyMaterial(ShaderPtr vertexPrg, ShaderPtr fragPrg)
  {
    m_skyboxMaterial            = MakeNewPtr<Material>();
    m_skyboxMaterial->m_cubeMap = GetHdri()->m_cubemap;
    m_skyboxMaterial->SetVertexShaderVal(vertexPrg);
    m_skyboxMaterial->SetFragmentShaderVal(fragPrg);
    m_skyboxMaterial->GetRenderState()->cullMode = CullingType::TwoSided;
    m_skyboxMaterial->Init();
  }

  XmlNode* SkyBase::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

  // Sky
  //////////////////////////////////////////

  TKDefineClass(Sky, SkyBase);

  Sky::Sky() {}

  Sky::~Sky() {}

  void Sky::Init()
  {
    if (m_initialized)
    {
      return;
    }

    SkyBase::Init();

    // Skybox material
    ShaderPtr vert = GetShaderManager()->Create<Shader>(ShaderPath("skyboxVert.shader", true));

    ShaderPtr frag = GetShaderManager()->Create<Shader>(ShaderPath("skyboxFrag.shader", true));

    ConstructSkyMaterial(vert, frag);

    m_initialized = true;
  }

  MaterialPtr Sky::GetSkyboxMaterial()
  {
    Init();

    HdriPtr hdri = GetHdri();
    hdri->Init();
    m_skyboxMaterial->m_cubeMap = hdri->m_cubemap;
    return m_skyboxMaterial;
  }

  void Sky::ParameterConstructor()
  {
    Super::ParameterConstructor();

    Hdri_Define(nullptr, SkyCategory.Name, SkyCategory.Priority, true, true);

    SkyWeakPtr self = Self<Sky>();
    ReGenerateIrradianceMap_Define(
        [self]() -> void
        {
          GetRenderSystem()->AddRenderTask({[self](Renderer* renderer) -> void
                                            {
                                              if (SkyPtr sky = self.lock())
                                              {
                                                if (HdriPtr hdr = sky->GetHdriVal())
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

    ParamVisible().m_exposed = false;

    // Update default.
    SetNameVal("Sky");
    SetIrradianceBakeFileVal(ConcatPaths({"ToolKit", TKDefaultHdri}));
  }

  void Sky::ParameterEventConstructor()
  {
    SkyBase::ParameterEventConstructor();

    ParamHdri().m_onValueChangedFn.clear();
    ParamHdri().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          EnvironmentComponentPtr environmentCom = GetComponent<EnvironmentComponent>();
          environmentCom->SetHdriVal(std::get<HdriPtr>(newVal));
        });
  }

  XmlNode* Sky::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

} // namespace ToolKit
