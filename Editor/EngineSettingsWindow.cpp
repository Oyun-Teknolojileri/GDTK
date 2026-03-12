/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "EngineSettingsWindow.h"

#include "App.h"
#include "CustomDataView.h"
#include "PopupWindows.h"
#include "UI.h"

#include <EngineSettings.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(EngineSettingsWindow, Window);

    EngineSettingsWindow::EngineSettingsWindow() { m_name = g_engineSettingsStr; }

    EngineSettingsWindow::~EngineSettingsWindow() {}

    // Utility function to calculate footer height. Update if footer content changes.
    float CalcFooterHeight()
    {
      const ImGuiStyle& style = ImGui::GetStyle();

      float sepH              = ImGui::GetTextLineHeightWithSpacing(); // SeparatorText row
      float checkH            = ImGui::GetFrameHeight();               // Checkbox row
      float buttonsH          = ImGui::GetFrameHeight();               // Buttons row

      // Vertical spacings between rows (SeparatorText -> Checkbox -> Buttons)
      float gaps              = style.ItemSpacing.y * 2.0f;

      // bottom padding
      float bottomPad         = style.ItemSpacing.y * 2.0f;

      return sepH + checkH + buttonsH + gaps + bottomPad;
    }

    void EngineSettingsWindow::Show()
    {
      EngineSettings& engineSettings = GetEngineSettings();
      GraphicSettingsPtr graphics    = engineSettings.m_graphics;

      ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_Once);
      if (ImGui::Begin(m_name.c_str(), &m_visible))
      {
        HandleStates();

        // Tighter footer reservation to leave more space for tabs
        float footerHeight = CalcFooterHeight();

        ImVec2 avail       = ImGui::GetContentRegionAvail();
        float childHeight  = glm::max(0.0f, avail.y - footerHeight);

        ImGui::BeginChild("RenderSettingsTabsChild", ImVec2(0, childHeight), false, ImGuiWindowFlags_None);
        if (ImGui::BeginTabBar("RenderSettingsTabs", ImGuiTabBarFlags_None))
        {
          ShowGraphicsTab();
          ShowShadowsTab();
          ShowPostProcessingTab();
          ImGui::EndTabBar();
        }
        ImGui::EndChild();

        // Footer
        ImGui::SeparatorText("Presets");
        ImGui::Checkbox("Save Shader Defines", &graphics->m_saveShaderDefines);
        UI::AddTooltipToLastItem("If enabled, shader defines are saved to the engine settings file.\n"
                                 "This prevents compiling all shader combinations, but requires "
                                 "recompiling shaders when a define is changed.");

        if (ImGui::Button("Save Settings"))
        {
          GetApp()->m_workspace.SerializeEngineSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Settings As"))
        {
          StringInputWindowPtr saveAsWindow = MakeNewPtr<StringInputWindow>("Save Settings As##SaveSettingsAs", true);
          saveAsWindow->m_inputLabel        = "Name";
          saveAsWindow->m_hint              = "Enter settings name";
          saveAsWindow->m_taskFn            = [](const String& val)
          { GetApp()->m_workspace.SerializeEngineSettings(val + ".settings"); };
          saveAsWindow->AddToUI();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Settings"))
        {
          m_showLoadWindow = true;
        }

        if (m_showLoadWindow)
        {
          static StringArray settingsFiles;
          static String selectedFile;
          if (settingsFiles.empty())
          {
            String path = GetApp()->m_workspace.GetConfigDirectory();
            for (const auto& entry : std::filesystem::directory_iterator(path))
            {
              if (entry.is_regular_file() && entry.path().extension() == ".settings")
              {
                String filename = entry.path().stem().u8string();
                if (filename != "Editor")
                {
                  settingsFiles.push_back(filename);
                }
              }
            }
            if (!settingsFiles.empty())
            {
              selectedFile = settingsFiles[0];
            }
          }

          ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(TK_FLT_MAX, TK_FLT_MAX));
          ImGui::Begin("Load Settings", &m_showLoadWindow, ImGuiWindowFlags_AlwaysAutoResize);
          if (settingsFiles.empty())
          {
            ImGui::Text("No .settings files found.");
          }
          else
          {
            for (const String& file : settingsFiles)
            {
              bool selected = (file == selectedFile);
              if (ImGui::Selectable(file.c_str(), selected))
              {
                selectedFile = file;
              }
            }
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
            if (ImGui::Button("Ok"))
            {
              GetApp()->m_workspace.DeSerializeEngineSettings(selectedFile);
              m_showLoadWindow = false;
              settingsFiles.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
              m_showLoadWindow = false;
              settingsFiles.clear();
            }
          }
          ImGui::End();
        }
      }
      ImGui::End();
    }

    void EngineSettingsWindow::ShowGraphicsTab()
    {
      EngineSettings& engineSettings = GetEngineSettings();
      GraphicSettingsPtr graphics    = engineSettings.m_graphics;

      // Graphics Tab
      if (ImGui::BeginTabItem("Graphics"))
      {
        static bool lockFps = true;
        if (ImGui::Checkbox("FPS Lock##1", &lockFps))
        {
          if (lockFps)
          {
            Main::GetInstance()->m_timing.Init(graphics->GetFPSVal());
          }
          else
          {
            Main::GetInstance()->m_timing.Init(9999);
          }
        }

        bool multiThreaded = graphics->GetMultiThreadedVal();
        if (ImGui::Checkbox("MultiThread##1", &multiThreaded))
        {
          graphics->SetMultiThreadedVal(multiThreaded);
        }

        bool hdrPipeline = graphics->GetHDRPipelineVal();
        if (ImGui::Checkbox("HDR Pipeline##1", &hdrPipeline))
        {
          graphics->SetHDRPipelineVal(hdrPipeline);
          GetApp()->ReInitViewports();
        }

        float renderScale = graphics->GetRenderResolutionScaleVal();
        if (ImGui::DragFloat("Resolution Multiplier", &renderScale, 0.05f, 0.25f, 1.0f))
        {
          graphics->SetRenderResolutionScaleVal(renderScale);
          GetApp()->ReInitViewports();
        }

        CustomDataView::ShowVariant(&graphics->ParamMSAA(),
                                    nullptr,
                                    [](const Value& vold, const Value& vnew) -> void { GetApp()->ReInitViewports(); });

        CustomDataView::ShowVariant(&graphics->ParamAnisotropicTextureFiltering(), nullptr);
        UI::AddTooltipToLastItem("Apply anisotropic filtering if the value is greater than 0. \nOnly effects all "
                                 "textures after editor restarted.");

        ImGui::EndTabItem(); // End Graphics Tab
      }
    }

    void EngineSettingsWindow::ShowShadowsTab()
    {
      EngineSettings& engineSettings = GetEngineSettings();
      GraphicSettingsPtr graphics    = engineSettings.m_graphics;
      ShadowSettingsPtr shadows      = graphics->m_shadows;

      // Shadows Tab
      if (ImGui::BeginTabItem("Shadows"))
      {
        bool use32BitShadowMap = shadows->GetUse32BitShadowMapVal();
        if (ImGui::Checkbox("Use high precision shadow maps", &use32BitShadowMap))
        {
          shadows->SetUse32BitShadowMapVal(use32BitShadowMap);
        }
        UI::AddTooltipToLastItem("Uses 32 bits floating point textures for shadow map generation.");

        CustomDataView::ShowVariant(&shadows->ParamShadowPCF(), nullptr);
        UI::AddTooltipToLastItem("Shadow PCF filtering tap count.\n"
                                 "Off: Single sample, no filtering.\n"
                                 "4 tap: ~3x3 kernel.\n"
                                 "9 tap: ~5x5 kernel.\n"
                                 "16 tap: ~7x7 kernel.");

        // Cascade count combo.
        {
          const char* itemNames[] = {"1", "2", "3", "4"};
          const int itemCount     = sizeof(itemNames) / sizeof(itemNames[0]);
          int currentItem         = shadows->GetCascadeCountVal() - 1;

          if (ImGui::BeginCombo("Cascade Count", itemNames[currentItem]))
          {
            for (int itemIndx = 0; itemIndx < itemCount; itemIndx++)
            {
              bool isSelected      = false;
              const char* itemName = itemNames[itemIndx];
              ImGui::Selectable(itemName, &isSelected);
              if (isSelected)
              {
                shadows->SetCascadeCountVal(itemIndx + 1);
              }
            }

            ImGui::EndCombo();
          }
        }

        Vec4 data            = shadows->GetCascadeDistancesVal();
        int lastCascadeIndex = shadows->GetCascadeCountVal() - 1;
        Vec2 contentSize     = ImGui::GetContentRegionAvail();
        float width          = contentSize.x * 0.95f / 4.0f;
        width                = glm::clamp(width, 10.0f, 100.0f);

        bool manualSplit     = !shadows->GetUseParallelSplitPartitioningVal();
        if (ImGui::Checkbox("Manual Split Cascades", &manualSplit))
        {
          shadows->SetUseParallelSplitPartitioningVal(!manualSplit);
        }

        if (!manualSplit)
        {
          ImGui::BeginDisabled();
        }

        bool cascadeInvalidated = false;
        for (int i = 0; i < 4; i++)
        {
          float val = data[i];
          if (i > lastCascadeIndex)
          {
            ImGui::BeginDisabled();
            val = 0.0f;
          }

          ImGui::PushID(i);
          ImGui::PushItemWidth(width);

          if (ImGui::DragFloat("##cascade", &val))
          {
            cascadeInvalidated = true;
            data[i]            = val;
          }
          String msg = std::to_string(i + 1) + ". cascade distance";
          UI::AddTooltipToLastItem(msg.c_str());

          ImGui::PopItemWidth();
          ImGui::PopID();

          if (i > lastCascadeIndex)
          {
            ImGui::EndDisabled();
          }

          if (i < 3)
          {
            ImGui::SameLine();
          }
        }

        if (!manualSplit)
        {
          ImGui::EndDisabled();
        }

        if (cascadeInvalidated)
        {
          shadows->SetCascadeDistancesVal(data);
        }

        bool useParallelSplit = shadows->GetUseParallelSplitPartitioningVal();
        if (ImGui::Checkbox("Parallel Split Cascades", &useParallelSplit))
        {
          shadows->SetUseParallelSplitPartitioningVal(useParallelSplit);
        }

        if (!useParallelSplit)
        {
          ImGui::BeginDisabled();
        }

        float parallelSplitLambda = shadows->GetParallelSplitLambdaVal();
        if (ImGui::DragFloat("Lambda", &parallelSplitLambda, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
          shadows->SetParallelSplitLambdaVal(parallelSplitLambda);
        }

        UI::AddTooltipToLastItem("Linear blending ratio between linear split and parallel split distances.");

        float shadowDistance = shadows->GetShadowMaxDistance();
        if (ImGui::DragFloat("Shadow Distance", &shadowDistance, 10.0f, 0.0f, 10000.0f, "%.2f"))
        {
          shadows->SetShadowMaxDistance(shadowDistance);
        }

        if (!shadows->GetUseParallelSplitPartitioningVal())
        {
          ImGui::EndDisabled();
        }

        bool stableShadowMap = shadows->GetStableShadowMapVal();
        if (ImGui::Checkbox("Stabilize Shadows", &stableShadowMap))
        {
          shadows->SetStableShadowMapVal(stableShadowMap);
        }
        UI::AddTooltipToLastItem("Prevents shimmering / swimming effects by wasting some shadow map resolution to "
                                 "prevent sub-pixel movements.");

        static bool highLightCascades = false;
        if (ImGui::Checkbox("Highlight Cascades", &highLightCascades))
        {
          ShaderPtr shader = GetShaderManager()->Create<Shader>(ShaderPath("defaultFragment.shader", true));
          shader->SetDefine("highlightCascades", highLightCascades ? "1" : "0");
        }
        UI::AddTooltipToLastItem("Highlights shadow cascades for debugging purpose.");

        ImGui::EndTabItem(); // End Shadows Tab
      }
    }

    void EngineSettingsWindow::ShowPostProcessingTab()
    {
      EngineSettings& engineSettings = GetEngineSettings();
      PostProcessingSettingsPtr pps  = engineSettings.m_postProcessing;

      // Post Processing Tab
      if (ImGui::BeginTabItem("Post Processing", nullptr))
      {
        if (ImGui::CollapsingHeader("ToneMapping"))
        {
          bool tonemappingEnabled = pps->GetTonemappingEnabledVal();
          if (ImGui::Checkbox("Enable Tonemapping", &tonemappingEnabled))
          {
            pps->SetTonemappingEnabledVal(tonemappingEnabled);
          }
          CustomDataView::ShowVariant(&pps->ParamTonemapperMode(), nullptr);
        }

        if (ImGui::CollapsingHeader("Bloom"))
        {
          bool bloomEnabled = pps->GetBloomEnabledVal();
          if (ImGui::Checkbox("Bloom##1", &bloomEnabled))
          {
            pps->SetBloomEnabledVal(bloomEnabled);
          }

          float bloomIntensity = pps->GetBloomIntensityVal();
          if (ImGui::DragFloat("Bloom Intensity", &bloomIntensity, 0.01f, 0.0f, 100.0f))
          {
            pps->SetBloomIntensityVal(bloomIntensity);
          }

          float bloomThreshold = pps->GetBloomThresholdVal();
          if (ImGui::DragFloat("Bloom Threshold", &bloomThreshold, 0.01f, 0.0f, 100.0f))
          {
            pps->SetBloomThresholdVal(bloomThreshold);
          }

          int bloomIterationCount = pps->GetBloomIterationCountVal();
          if (ImGui::InputInt("Bloom Iteration Count", &bloomIterationCount, 1, 2))
          {
            pps->SetBloomIterationCountVal(bloomIterationCount);
          }
        }

        if (ImGui::CollapsingHeader("Depth of Field"))
        {
          bool dofEnabled = pps->GetDepthOfFieldEnabledVal();
          if (ImGui::Checkbox("Depth of Field##1", &dofEnabled))
          {
            pps->SetDepthOfFieldEnabledVal(dofEnabled);
          }

          ImGui::BeginDisabled(!dofEnabled);

          float dofFocusPoint = pps->GetFocusPointVal();
          if (ImGui::DragFloat("Focus Point", &dofFocusPoint, 0.1f, 0.0f, 100.0f))
          {
            pps->SetFocusPointVal(dofFocusPoint);
          }

          float dofFocusScale = pps->GetFocusScaleVal();
          if (ImGui::DragFloat("Focus Scale", &dofFocusScale, 0.01f, 1.0f, 200.0f))
          {
            pps->SetFocusScaleVal(dofFocusScale);
          }

          const char* items[] = {"Low", "Normal", "High"};
          uint itemCount      = sizeof(items) / sizeof(items[0]);
          int blurQuality     = pps->GetDofBlurQualityVal();
          if (ImGui::BeginCombo("Blur Quality", items[blurQuality]))
          {
            for (uint itemIndx = 0; itemIndx < itemCount; itemIndx++)
            {
              bool isSelected      = false;
              const char* itemName = items[itemIndx];
              ImGui::Selectable(itemName, &isSelected);
              if (isSelected)
              {
                pps->SetDofBlurQualityVal(itemIndx);
              }
            }

            ImGui::EndCombo();
          }
          ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Ambient Occlusion"))
        {
          bool ssaoEnabled = pps->GetSSAOEnabledVal();
          if (ImGui::Checkbox("SSAO##1", &ssaoEnabled))
          {
            pps->SetSSAOEnabledVal(ssaoEnabled);
          }
          ImGui::BeginDisabled(!ssaoEnabled);

          float ssaoRadius = pps->GetSSAORadiusVal();
          if (ImGui::DragFloat("Radius", &ssaoRadius, 0.001f, 0.0f, 1.0f))
          {
            pps->SetSSAORadiusVal(ssaoRadius);
          }

          float ssaoSpread = pps->GetSSAOSpreadVal();
          if (ImGui::DragFloat("Spread", &ssaoSpread, 0.001f, 0.0f, 1.0f))
          {
            pps->SetSSAOSpreadVal(ssaoSpread);
          }

          float ssaoBias = pps->GetSSAOBiasVal();
          if (ImGui::DragFloat("Bias", &ssaoBias, 0.001f, 0.0f, 1.0f))
          {
            pps->SetSSAOBiasVal(ssaoBias);
          }

          int ssaoKernelSize = pps->GetSSAOKernelSizeVal();
          if (ImGui::DragInt("KernelSize", &ssaoKernelSize, 1, 8, 128))
          {
            pps->SetSSAOKernelSizeVal(ssaoKernelSize);
          }

          ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Anti Aliasing"))
        {
          bool fxaaEnabled = pps->GetFXAAEnabledVal();
          if (ImGui::Checkbox("FXAA##1", &fxaaEnabled))
          {
            pps->SetFXAAEnabledVal(fxaaEnabled);
          }
        }

        ImGui::EndTabItem(); // End Post Processing Tab
      }
    }

  } // namespace Editor
} // namespace ToolKit} // namespace ToolKit