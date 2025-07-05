/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "EngineSettings.h"

#include "DofPass.h"
#include "GammaTonemapFxaaPass.h"
#include "MathUtil.h"
#include "PluginManager.h"
#include "RenderSystem.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  // WindowSettings
  //////////////////////////////////////////

  TKDefineClass(WindowSettings, Object);

  void WindowSettings::ParameterConstructor()
  {
    Super::ParameterConstructor();

    Name_Define("ToolKit", "WindowSettings", 0, 0, 0);
    Width_Define(1280u, "WindowSettings", 0, 0, 0);
    Height_Define(720u, "WindowSettings", 0, 0, 0);
    FullScreen_Define(false, "WindowSettings", 0, 0, 0);
  }

  TKDefineClass(ShadowSettings, Object);

  // ShadowSettings
  //////////////////////////////////////////

  void ShadowSettings::ParameterConstructor()
  {
    Super::ParameterConstructor();

    MultiChoiceVariant mcv = {
        {CreateMultiChoiceParameter("1", 1),
         CreateMultiChoiceParameter("9", 9),
         CreateMultiChoiceParameter("25", 25),
         CreateMultiChoiceParameter("49", 49)},
        1
    };

    ShadowSamples_Define(mcv, "ShadowSettings", 0, true, true);

    CascadeCount_Define(4, "ShadowSettings", 0, 0, 0);
    CascadeDistances_Define(Vec4(10.0f, 20.0f, 50.0f, 100.0f), "ShadowSettings", 0, 0, 0);
    ShadowMinDistance_Define(1.0f, "ShadowSettings", 0, 0, 0);
    UseParallelSplitPartitioning_Define(true, "ShadowSettings", 0, 0, 0);
    ParallelSplitLambda_Define(0.5f, "ShadowSettings", 0, 0, 0);
    StableShadowMap_Define(false, "ShadowSettings", 0, 0, 0);
    UseEVSM4_Define(false, "ShadowSettings", 0, 0, 0);
    Use32BitShadowMap_Define(false, "ShadowSettings", 0, 0, 0);
  }

  void ShadowSettings::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    m_updateGraphicConstantsFn = [this](Value& oldVal, Value& newVal)
    {
      RenderSystem* rsys = GetRenderSystem();
      if (rsys == nullptr)
      {
        TK_ERR("Render system is not initialized, graphics constants can't be updated.");
        return;
      }

      // Update buffer on next frame.
      rsys->AddRenderTask({[](Renderer* renderer) -> void { renderer->InvalidateGraphicsConstants(); }});
    };

    ParamCascadeCount().m_onValueChangedFn.push_back(m_updateGraphicConstantsFn);
    ParamUseParallelSplitPartitioning().m_onValueChangedFn.push_back(m_updateGraphicConstantsFn);

    // Try preventing costly gpu buffer map. This value is constantly being updated from shadow pass.
    ParamCascadeDistances().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          Vec4 oldV = std::get<Vec4>(oldVal);
          Vec4 newV = std::get<Vec4>(newVal);

          if (!VecAllEqual(oldV, newV))
          {
            m_updateGraphicConstantsFn(oldVal, newVal);
          }
        });
  }

  void ShadowSettings::PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    Super::PostDeSerializeImp(info, parent);

    Value tmp(0);
    m_updateGraphicConstantsFn(tmp, tmp);
  }

  // GraphicSettings
  //////////////////////////////////////////

  TKDefineClass(GraphicSettings, Object);

  void GraphicSettings::ParameterConstructor()
  {
    Super::ParameterConstructor();

    MultiChoiceVariant msaaMcv = {
        {CreateMultiChoiceParameter("0", 0),
         CreateMultiChoiceParameter("2", 2),
         CreateMultiChoiceParameter("4", 4),
         CreateMultiChoiceParameter("8", 8)},
        1
    };
    MSAA_Define(msaaMcv, "GraphicSettings", 0, true, true);

    MultiChoiceVariant anisotropicMcv = {
        {CreateMultiChoiceParameter("0", 0),
         CreateMultiChoiceParameter("2", 2),
         CreateMultiChoiceParameter("4", 4),
         CreateMultiChoiceParameter("8", 8),
         CreateMultiChoiceParameter("16", 16)},
        1
    };
    AnisotropicTextureFiltering_Define(anisotropicMcv, "GraphicSettings", 0, true, true);

    FPS_Define(60, "GraphicSettings", 0, 0, 0);
    EnableGpuTimer_Define(false, "GraphicSettings", 0, 0, 0);
    HDRPipeline_Define(true, "GraphicSettings", 0, 0, 0);
    RenderResolutionScale_Define(1.0f, "GraphicSettings", 0, 0, 0);
  }

  // PostProcessingSettings
  //////////////////////////////////////////

  TKDefineClass(PostProcessingSettings, Object);

  void PostProcessingSettings::ParameterConstructor()
  {
    Super::ParameterConstructor();

    MultiChoiceVariant toneMapping = {
        {
         CreateMultiChoiceParameter("Reinhard", 0),
         CreateMultiChoiceParameter("ACES", 1),
         },
        1
    };
    TonemapperMode_Define(toneMapping, "PostProcessingSettings", 0, true, true);
    TonemappingEnabled_Define(true, "PostProcessingSettings", 0, 0, 0);

    BloomEnabled_Define(false, "PostProcessingSettings", 0, 0, 0);
    BloomIntensity_Define(1.0f, "PostProcessingSettings", 0, 0, 0);
    BloomThreshold_Define(1.0f, "PostProcessingSettings", 0, 0, 0);
    BloomIterationCount_Define(5, "PostProcessingSettings", 0, 0, 0);
    GammaCorrectionEnabled_Define(true, "PostProcessingSettings", 0, 0, 0);
    Gamma_Define(2.2f, "PostProcessingSettings", 0, 0, 0);
    SSAOEnabled_Define(false, "PostProcessingSettings", 0, 0, 0);
    SSAORadius_Define(1.0f, "PostProcessingSettings", 0, 0, 0);
    SSAOBias_Define(0.025f, "PostProcessingSettings", 0, 0, 0);
    SSAOSpread_Define(1.0f, "PostProcessingSettings", 0, 0, 0);
    SSAOKernelSize_Define(8, "PostProcessingSettings", 0, 0, 0);
    DepthOfFieldEnabled_Define(false, "PostProcessingSettings", 0, 0, 0);
    FocusPoint_Define(10.5f, "PostProcessingSettings", 0, 0, 0);
    FocusScale_Define(1.5f, "PostProcessingSettings", 0, 0, 0);
    DofBlurQuality_Define((int) DoFQuality::High, "PostProcessingSettings", 0, 0, 0);
    FXAAEnabled_Define(true, "PostProcessingSettings", 0, 0, 0);
  }

  // EngineSettings
  //////////////////////////////////////////

  EngineSettings::EngineSettings()
  {
    m_window         = MakeNewPtr<WindowSettings>();
    m_graphics       = MakeNewPtr<GraphicSettings>();
    m_postProcessing = MakeNewPtr<PostProcessingSettings>();
  }

  XmlNode* EngineSettings::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    if (doc == nullptr)
    {
      return parent;
    }

    XmlNode* settingsNode = CreateXmlNode(doc, "Settings", nullptr);
    WriteAttr(settingsNode, doc, "version", TKVersionStr);

    m_window->Serialize(doc, settingsNode);
    m_graphics->Serialize(doc, settingsNode);
    m_graphics->m_shadows->Serialize(doc, settingsNode);

    XmlNode* pluginNode = CreateXmlNode(doc, "Plugins", settingsNode);
    if (PluginManager* plugMan = GetPluginManager())
    {
      for (const PluginRegister& reg : plugMan->m_storage)
      {
        if (reg.m_loaded)
        {
          if (reg.m_plugin->GetType() != PluginType::Game)
          {
            XmlNode* plugin = CreateXmlNode(doc, "Plugin", pluginNode);
            WriteAttr(plugin, doc, "name", reg.m_name);
          }
        }
      }
    }

    return settingsNode;
  }

  XmlNode* EngineSettings::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlDocument* doc      = info.Document;
    XmlNode* settingsNode = doc->first_node("Settings");
    XmlNode* objNode      = settingsNode ? settingsNode->first_node(Object::StaticClass()->Name.c_str()) : nullptr;

    if (objNode == nullptr)
    {
      TK_ERR("EngineSettings: No settings node found in the document.");
      return nullptr;
    }

    do
    {
      String className;
      ReadAttr(objNode, XmlObjectClassAttr.data(), className);

      if (className == WindowSettings::StaticClass()->Name)
      {
        m_window->DeSerialize(info, objNode);
      }
      else if (className == GraphicSettings::StaticClass()->Name)
      {
        m_graphics->DeSerialize(info, objNode);
      }
      else if (className == ShadowSettings::StaticClass()->Name)
      {
        m_graphics->m_shadows->DeSerialize(info, objNode);
      }
    } while (objNode = objNode->next_sibling());

    if (XmlNode* pluginNode = settingsNode->first_node("Plugins"))
    {
      XmlNode* plugin = pluginNode->first_node();
      while (plugin)
      {
        String pluginName;
        ReadAttr(plugin, "name", pluginName);
        m_loadedPlugins.push_back(pluginName);

        plugin = plugin->next_sibling();
      }
    }

    return settingsNode;
  }

  void EngineSettings::Save(const String& path)
  {
    std::ofstream file;
    file.open(path.c_str(), std::ios::out | std::ios::trunc);
    assert(file.is_open());

    if (file.is_open())
    {
      XmlDocument* lclDoc = new XmlDocument();
      SerializeImp(lclDoc, nullptr);

      std::string xml;
      rapidxml::print(std::back_inserter(xml), *lclDoc);
      file << xml;
      file.close();
      lclDoc->clear();

      SafeDel(lclDoc);
    }
  }

  void EngineSettings::Load(const String& path)
  {
    XmlFile* lclFile    = new XmlFile(path.c_str());
    XmlDocument* lclDoc = new XmlDocument();
    lclDoc->parse<0>(lclFile->data());

    SerializationFileInfo info;
    info.File     = path;
    info.Document = lclDoc;

    DeSerializeImp(info, nullptr);

    SafeDel(lclFile);
    SafeDel(lclDoc);
  }

} // namespace ToolKit