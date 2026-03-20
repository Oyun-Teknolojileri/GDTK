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

    MultiChoiceVariant pcfMcv = {
        {CreateMultiChoiceParameter("Off", 0),
         CreateMultiChoiceParameter("4 tap", 4),
         CreateMultiChoiceParameter("9 tap", 9),
         CreateMultiChoiceParameter("16 tap", 16)},
        1
    };

    ShadowPCF_Define(pcfMcv, "ShadowSettings", 0, true, true);

    CascadeCount_Define(4, "ShadowSettings", 0, 0, 0);
    CascadeDistances_Define(Vec4(10.0f, 20.0f, 50.0f, 100.0f), "ShadowSettings", 0, 0, 0);
    ShadowMinDistance_Define(1.0f, "ShadowSettings", 0, 0, 0);
    UseParallelSplitPartitioning_Define(true, "ShadowSettings", 0, 0, 0);
    ParallelSplitLambda_Define(0.5f, "ShadowSettings", 0, 0, 0);
    StableShadowMap_Define(false, "ShadowSettings", 0, 0, 0);
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

  MultiChoiceVariant gDefaultMsaaMcv = {
      {CreateMultiChoiceParameter("1x", (int) MsaaSampleCount::x1),
       CreateMultiChoiceParameter("2x", (int) MsaaSampleCount::x2),
       CreateMultiChoiceParameter("4x", (int) MsaaSampleCount::x4),
       CreateMultiChoiceParameter("8x", (int) MsaaSampleCount::x8)},
      1
  };

  void GraphicSettings::ParameterConstructor()
  {
    Super::ParameterConstructor();

    MSAA_Define(gDefaultMsaaMcv, "GraphicSettings", 0, true, true);

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
    HDRPipeline_Define(true, "GraphicSettings", 0, 0, 0);
    RenderResolutionScale_Define(1.0f, "GraphicSettings", 0, 0, 0);
    MultiThreaded_Define(true, "GraphicSettings", 0, 0, 0);
  }

  void GraphicSettings::ParameterEventConstructor()
  {
    ParamMultiThreaded().m_onValueChangedFn.push_back(
        [](Value& oldVal, Value& newVal) -> void
        {
          bool multiThreaded              = std::get<bool>(newVal);
          Main::GetInstance()->m_threaded = multiThreaded;
        });
  }

  void GraphicSettings::PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    Super::PostDeSerializeImp(info, parent);

    // Try to set a meaningful value for old msaa settings.
    if (m_version <= TKV049)
    {
      MsaaSampleCount msaaVal = GetMSAAVal().GetEnum<MsaaSampleCount>();

      switch (msaaVal)
      {
      case MsaaSampleCount::x1:
      case MsaaSampleCount::x2:
      case MsaaSampleCount::x4:
      case MsaaSampleCount::x8:
        break;
      default:
        msaaVal = MsaaSampleCount::x1;
      }

      MultiChoiceVariant msaa = gDefaultMsaaMcv;
      msaa.SetEnum(msaaVal);
      SetMSAAVal(msaa);
    }
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

  // ShaderSettings
  //////////////////////////////////////////

  TKDefineClass(ShaderSettings, Object);

  void ShaderSettings::SyncDefinesForShader(const String& shaderPath, ShaderDefineArray& defines) const
  {
    auto it = m_shaderDefines.find(shaderPath);
    if (it == m_shaderDefines.end())
    {
      return;
    }

    for (const ShaderDefine& def : it->second)
    {
      auto existing = std::find(defines.begin(), defines.end(), def);
      if (existing != defines.end())
      {
        *existing = def;
      }
      else
      {
        defines.push_back(def);
      }
    }
  }

  void ShaderSettings::SetShaderDefine(const String& shaderPath, const ShaderDefine& define)
  {
    ShaderDefineArray& defines = m_shaderDefines[shaderPath];
    auto it                    = std::find(defines.begin(), defines.end(), define);
    if (it == defines.end())
    {
      defines.push_back(define); // Add.
    }
    else
    {
      *it = define; // or update.
    }
  }

  XmlNode* ShaderSettings::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    parent                = Super::SerializeImp(doc, parent);
    XmlNode* settingsNode = CreateXmlNode(doc, "ShaderSettings", parent);
    for (const auto& entry : m_shaderDefines)
    {
      XmlNode* shaderNode = CreateXmlNode(doc, "shader", settingsNode);
      WriteAttr(shaderNode, doc, "file", entry.first);
      for (const ShaderDefine& def : entry.second)
      {
        XmlNode* defineNode = CreateXmlNode(doc, "define", shaderNode);
        WriteAttr(defineNode, doc, "name", def.define);

        // Merge variants.
        String result;
        for (size_t i = 0; i < def.variants.size(); i++)
        {
          result += def.variants[i];
          if (i != def.variants.size() - 1)
          {
            result += ", ";
          }
        }

        WriteAttr(defineNode, doc, "val", result);
      }
    }

    return parent;
  }

  XmlNode* ShaderSettings::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    parent = Super::DeSerializeImp(info, parent);
    if (XmlNode* settingsNode = parent->first_node("ShaderSettings"))
    {
      for (XmlNode* shaderNode = settingsNode->first_node("shader"); shaderNode;
           shaderNode          = shaderNode->next_sibling("shader"))
      {
        String file;
        ReadAttr(shaderNode, "file", file);
        ShaderDefineArray defines;
        for (XmlNode* defineNode = shaderNode->first_node("define"); defineNode;
             defineNode          = defineNode->next_sibling("define"))
        {
          ShaderDefine def;
          ReadAttr(defineNode, "name", def.define);
          String val;
          ReadAttr(defineNode, "val", val);
          Split(val, ", ", def.variants);
          defines.push_back(def);
        }
        m_shaderDefines[file] = defines;
      }
    }

    return parent;
  }

  // EngineSettings
  //////////////////////////////////////////

  EngineSettings::EngineSettings()
  {
    m_window         = MakeNewPtr<WindowSettings>();
    m_graphics       = MakeNewPtr<GraphicSettings>();
    m_postProcessing = MakeNewPtr<PostProcessingSettings>();
    m_shaderSettings = MakeNewPtr<ShaderSettings>();
  }

  XmlNode* EngineSettings::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    if (doc == nullptr)
    {
      return parent;
    }

    XmlNode* settingsNode = CreateXmlNode(doc, "Settings", nullptr);
    WriteAttr(settingsNode, doc, XmlVersion.data(), TKVersionStr);

    m_window->Serialize(doc, settingsNode);
    m_graphics->Serialize(doc, settingsNode);
    m_graphics->m_shadows->Serialize(doc, settingsNode);
    m_shaderSettings->Serialize(doc, settingsNode);

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
      else if (className == ShaderSettings::StaticClass()->Name)
      {
        m_shaderSettings->DeSerialize(info, objNode);
      }
    } while (objNode = objNode->next_sibling());

    if (XmlNode* pluginNode = settingsNode->first_node("Plugins"))
    {
      m_loadedPlugins.clear();
      XmlNode* plugin = pluginNode->first_node();
      while (plugin)
      {
        String pluginName;
        ReadAttr(plugin, "name", pluginName);
        m_loadedPlugins.insert(pluginName);

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
      // Set shader defines for current graphics settings.
      if (m_graphics->m_saveShaderDefines)
      {
        SetShaderSettings();
      }

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

  void EngineSettings::SetShaderSettings()
  {
    // Default fragment shader defines.
    String file = ShaderPath("defaultFragment" + SHADER, true);

    ShaderDefine def;
    def.define   = "highlightCascades";
    def.variants = {"0"};
    m_shaderSettings->SetShaderDefine(file, def);

    def.define   = "SMFormat16Bit";
    def.variants = {m_graphics->m_shadows->GetUse32BitShadowMapVal() ? "0" : "1"};
    m_shaderSettings->SetShaderDefine(file, def);

    def.define   = "ShadowPCF";
    def.variants = {std::to_string(m_graphics->m_shadows->GetShadowPCFVal().GetValue<int>())};
    m_shaderSettings->SetShaderDefine(file, def);

    // Gauss blur defines.
    // The defines below saves 2*3 combination.
    file         = ShaderPath("gaussBlur7x1Frag" + SHADER, true);
    def.define   = "KernelSize";
    def.variants = {"7"};
    m_shaderSettings->SetShaderDefine(file, def);

    def.define   = "TextureArray";
    def.variants = {"0"};
    m_shaderSettings->SetShaderDefine(file, def);

    // Shadow defines.
    file         = ShaderPath("orthogonalDepthFrag" + SHADER, true);

    def.define   = "SMFormat16Bit";
    def.variants = {m_graphics->m_shadows->GetUse32BitShadowMapVal() ? "0" : "1"};
    m_shaderSettings->SetShaderDefine(file, def);

    file         = ShaderPath("perspectiveDepthFrag" + SHADER, true);

    def.define   = "SMFormat16Bit";
    def.variants = {m_graphics->m_shadows->GetUse32BitShadowMapVal() ? "0" : "1"};
    m_shaderSettings->SetShaderDefine(file, def);
  }

} // namespace ToolKit