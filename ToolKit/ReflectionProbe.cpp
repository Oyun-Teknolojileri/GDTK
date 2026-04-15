/*
 /*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "ReflectionProbe.h"

#include "ForwardSceneRenderPath.h"
#include "MathUtil.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "Scene.h"
#include "Texture.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  TKDefineClass(ReflectionProbe, Entity);

  ReflectionProbe::ReflectionProbe() { m_partOfAABBTree = false; }

  ReflectionProbe::~ReflectionProbe() {}

  void ReflectionProbe::NativeConstruct() { Super::NativeConstruct(); }

  void ReflectionProbe::ComponentConstructor()
  {
    Super::ComponentConstructor();
    AddComponent<EnvironmentComponent>();
  }

  void ReflectionProbe::Init()
  {
    if (m_initialized)
    {
      return;
    }

    EnvironmentComponentPtr envComp = GetComponent<EnvironmentComponent>();
    if (envComp == nullptr)
    {
      envComp = AddComponent<EnvironmentComponent>();
    }

    HdriPtr hdri = envComp->GetHdriVal();
    if (hdri == nullptr)
    {
      hdri = MakeNewPtr<Hdri>();
      envComp->SetHdriVal(hdri);
    }

    envComp->OwnerEntity(Self<Entity>());
    envComp->Init(false);

    m_initialized = true;
  }

  void ReflectionProbe::CaptureEnvironment()
  {
    EnvironmentComponentPtr envComp = GetComponent<EnvironmentComponent>();
    if (envComp == nullptr)
    {
      return;
    }

    uint res            = (uint) GetCaptureResolutionVal().GetValue<int>();

    // Local-space volume parameters.
    Vec3 offset         = envComp->GetPositionOffsetVal();
    Vec3 half           = envComp->GetSizeVal() * 0.5f;

    Mat4 worldTransform = m_node->GetTransform(TransformationSpace::TS_WORLD);
    float extraFar      = GetCaptureFarVal();

    // Cubemap face normals in local space: +X, -X, +Y, -Y, +Z, -Z.
    static const Vec3 faceNormals[6] =
        {Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1)};

    // Compute per-face far clip distance in local space.
    float minDist = 0.01f;
    float perFaceClipDist[6];
    for (int i = 0; i < 6; i++)
    {
      Vec3 edge          = faceNormals[i] * half;
      float dist         = glm::abs(glm::dot(faceNormals[i], edge) - glm::dot(faceNormals[i], offset));
      perFaceClipDist[i] = glm::max(dist + extraFar, minDist);
    }

    EnvironmentComponent* envRaw = envComp.get();
    GetRenderSystem()->AddRenderTask(
        {[envRaw, worldTransform, offset, minDist, res, perFaceClipDist](Renderer* renderer) -> void
         {
           // Disable self-illumination during capture to prevent feedback loop.
           bool wasIlluminate = envRaw->GetIlluminateVal();
           envRaw->SetIlluminateVal(false);

           // Create a temporary render path for the capture.
           ForwardSceneRenderPath capturePath;
           capturePath.m_params.Scene = GetSceneManager()->GetCurrentScene();

           CubeMapPtr cubemap =
               renderer->RenderToCubeMap(&capturePath, worldTransform, offset, minDist, 1000.0f, res, perFaceClipDist);

           // Restore illuminate state.
           envRaw->SetIlluminateVal(wasIlluminate);

           // Create a dynamic HDRI and assign the captured cubemap.
           HdriPtr hdri    = MakeNewPtr<Hdri>();
           hdri->m_cubemap = cubemap;
           hdri->GenerateIrradianceCaches(renderer);
           hdri->m_initiated = true;

           envRaw->SetHdriVal(hdri);
         }});
  }

  EnvironmentComponentPtr ReflectionProbe::GetEnvironmentComponent() const { return GetComponent<EnvironmentComponent>(); }

  const BoundingBox& ReflectionProbe::GetBoundingBox(bool inWorld) { return unitBox; }

  void ReflectionProbe::ParameterConstructor()
  {
    Super::ParameterConstructor();

    ParallaxCorrection_Define(false,
                              ReflectionProbeCategory.Name,
                              ReflectionProbeCategory.Priority,
                              true,
                              true);

    Interior_Define(false, ReflectionProbeCategory.Name, ReflectionProbeCategory.Priority, true, true);

    Fade_Define(1.0f,
                ReflectionProbeCategory.Name,
                ReflectionProbeCategory.Priority,
                true,
                true,
                {false, true, 0.0f, 100000.0f, 0.1f});

    CaptureFar_Define(0.0f,
                      ReflectionProbeCategory.Name,
                      ReflectionProbeCategory.Priority,
                      true,
                      true,
                      {false, true, 0.0f, 100000.0f, 1.0f});

    MultiChoiceVariant captureResMcv;
    captureResMcv.Choices.push_back(CreateMultiChoiceParameter("128", 128));
    captureResMcv.Choices.push_back(CreateMultiChoiceParameter("256", 256));
    captureResMcv.Choices.push_back(CreateMultiChoiceParameter("512", 512));
    captureResMcv.CurrentVal.Index = 1;
    CaptureResolution_Define(captureResMcv,
                             ReflectionProbeCategory.Name,
                             ReflectionProbeCategory.Priority,
                             true,
                             true);

    SetNameVal("ReflectionProbe");
  }

  void ReflectionProbe::ParameterEventConstructor() { Super::ParameterEventConstructor(); }

  XmlNode* ReflectionProbe::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

  XmlNode* ReflectionProbe::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlNode* nttNode = Super::DeSerializeImp(info, parent);
    return nttNode->first_node(StaticClass()->Name.c_str());
  }

} // namespace ToolKit
