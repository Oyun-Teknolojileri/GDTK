/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "RenderSettingsWindow.h"

#include "App.h"
#include "CustomDataView.h"

#include <EngineSettings.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(RenderSettingsWindow, Window);

    RenderSettingsWindow::RenderSettingsWindow() { m_name = g_renderSettings; }

    RenderSettingsWindow::~RenderSettingsWindow() {}

    void RenderSettingsWindow::Show()
    {
      EngineSettings& engineSettings = GetEngineSettings();
      GraphicSettingsPtr graphics    = engineSettings.m_graphics;
      ShadowSettingsPtr shadows      = graphics->m_shadows;

      ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_Once);
      if (ImGui::Begin(m_name.c_str(), &m_visible))
      {
        HandleStates();

        PostProcessingSettingsPtr pps = engineSettings.m_postProcessing;

        ImGui::SeparatorText("Post Process");

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

        ImGui::SeparatorText("General");

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

        static bool multiThreaded = true;
        if (ImGui::Checkbox("MultiThread##1", &multiThreaded))
        {
          if (multiThreaded)
          {
            Main::GetInstance()->m_threaded = true;
          }
          else
          {
            Main::GetInstance()->m_threaded = false;
          }
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

        ImGui::SeparatorText("Multi Sample Anti Aliasing");
        CustomDataView::ShowVariant(&graphics->ParamMSAA(),
                                    nullptr,
                                    [](const Value& vold, const Value& vnew) -> void { GetApp()->ReInitViewports(); });

        ImGui::SeparatorText("Shadows");

        bool evsm4 = shadows->GetUseEVSM4Val();
        if (ImGui::RadioButton("Use EVSM2", !evsm4))
        {
          shadows->SetUseEVSM4Val(false);
        }
        UI::AddTooltipToLastItem("Exponential variance shadow mapping with positive component.");

        ImGui::SameLine();

        if (ImGui::RadioButton("Use EVSM4", evsm4))
        {
          shadows->SetUseEVSM4Val(true);
        }
        UI::AddTooltipToLastItem("Exponential variance shadow mapping with positive and negative component."
                                 "\nRequires more shadow map memory, but yields softer shadows.");

        bool use32BitShadowMap = shadows->GetUse32BitShadowMapVal();
        if (ImGui::Checkbox("Use high precision shadow maps", &use32BitShadowMap))
        {
          shadows->SetUse32BitShadowMapVal(use32BitShadowMap);
        }
        UI::AddTooltipToLastItem("Uses 32 bits floating point textures for shadow map generation.");

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

        // Show shadow sample multi choice.
        CustomDataView::ShowVariant(&shadows->ParamShadowSamples(), nullptr);
        UI::AddTooltipToLastItem("Number of samples taken from shadow map to calculate shadow factor.");

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

        ImGui::SeparatorText("Global Texture Settings");

        // Anisotropy combo.
        CustomDataView::ShowVariant(&graphics->ParamAnisotropicTextureFiltering(), nullptr);
        UI::AddTooltipToLastItem("Apply anisotropic filtering if the value is greater than 0. \nOnly effects all "
                                 "textures after editor restarted.");
      }
      ImGui::End();
    }

  } // namespace Editor
} // namespace ToolKit