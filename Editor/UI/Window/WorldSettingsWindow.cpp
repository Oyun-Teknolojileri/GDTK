/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "WorldSettingsWindow.h"

#include "CustomDataView.h"
#include "EditorTypes.h"
#include "UI.h"
#include <EngineSettings.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(WorldSettingsWindow, Window);

    WorldSettingsWindow::WorldSettingsWindow() { m_name = g_worldSettingsStr; }

    WorldSettingsWindow::~WorldSettingsWindow() {}

    bool WorldSettingsWindow::ShowSection(const char* label)
    {
      return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    }

    void WorldSettingsWindow::Show()
    {
      EngineSettings& engineSettings = GetEngineSettings();
      PostProcessingSettingsPtr pps  = engineSettings.m_postProcessing;

      ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_Once);
      if (ImGui::Begin(m_name.c_str(), &m_visible))
      {
        HandleStates();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("WorldSettingsChild", ImVec2(0, avail.y), false, ImGuiWindowFlags_None);

      if (ShowSection("ToneMapping"))
      {
        bool tonemappingEnabled = pps->GetTonemappingEnabledVal();
        if (ImGui::Checkbox("Enable Tonemapping", &tonemappingEnabled))
        {
          pps->SetTonemappingEnabledVal(tonemappingEnabled);
        }
        CustomDataView::ShowVariant(&pps->ParamTonemapperMode(), nullptr);
      }

      if (ShowSection("Bloom"))
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

      if (ShowSection("Depth of Field"))
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

      if (ShowSection("Ambient Occlusion"))
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

        CustomDataView::ShowVariant(&pps->ParamSSAOKernelSize(), nullptr);

        bool ssaoHalfRes = pps->GetSSAOHalfResolutionVal();
        if (ImGui::Checkbox("Half Resolution##ssao", &ssaoHalfRes))
        {
          pps->SetSSAOHalfResolutionVal(ssaoHalfRes);
        }

        ImGui::EndDisabled();
      }

      if (ShowSection("Screen Space Reflections"))
      {
        bool ssrEnabled = pps->GetSSREnabledVal();
        if (ImGui::Checkbox("SSR##1", &ssrEnabled))
        {
          pps->SetSSREnabledVal(ssrEnabled);
        }
        ImGui::BeginDisabled(!ssrEnabled);

        float ssrIntensity = pps->GetSSRIntensityVal();
        if (ImGui::DragFloat("Intensity", &ssrIntensity, 0.01f, 0.0f, 1.0f))
        {
          pps->SetSSRIntensityVal(ssrIntensity);
        }

        float ssrMaxDistance = pps->GetSSRMaxDistanceVal();
        if (ImGui::DragFloat("Max Distance", &ssrMaxDistance, 0.1f, 0.1f, 500.0f))
        {
          pps->SetSSRMaxDistanceVal(ssrMaxDistance);
        }

        float ssrThickness = pps->GetSSRThicknessVal();
        if (ImGui::DragFloat("Thickness", &ssrThickness, 0.01f, 0.01f, 5.0f))
        {
          pps->SetSSRThicknessVal(ssrThickness);
        }

        // The fragment shader is compiled with a loop bound from {8, 16, 32, 64}, so the setting is one
        // of those four. A dropdown keeps the two in step: a free value would be honoured by the march
        // but the loop could only be bounded by the next variant above it.
        static const char* ssrStepCountNames[] = {"8", "16", "32", "64"};
        static const int ssrStepCountValues[]  = {8, 16, 32, 64};

        int ssrStepCount = pps->GetSSRStepCountVal();
        int ssrStepIndex = 0;
        int ssrStepDelta = ssrStepCount > ssrStepCountValues[0] ? ssrStepCount - ssrStepCountValues[0]
                                                                : ssrStepCountValues[0] - ssrStepCount;
        for (int i = 1; i < IM_ARRAYSIZE(ssrStepCountValues); ++i)
        {
          const int delta = ssrStepCount > ssrStepCountValues[i] ? ssrStepCount - ssrStepCountValues[i]
                                                                 : ssrStepCountValues[i] - ssrStepCount;
          if (delta < ssrStepDelta)
          {
            ssrStepDelta = delta;
            ssrStepIndex = i;
          }
        }

        if (ImGui::Combo("Steps", &ssrStepIndex, ssrStepCountNames, IM_ARRAYSIZE(ssrStepCountNames)))
        {
          pps->SetSSRStepCountVal(ssrStepCountValues[ssrStepIndex]);
        }

        float ssrRoughnessCutoff = pps->GetSSRRoughnessCutoffVal();
        if (ImGui::DragFloat("Roughness Cutoff", &ssrRoughnessCutoff, 0.01f, 0.0f, 1.0f))
        {
          pps->SetSSRRoughnessCutoffVal(ssrRoughnessCutoff);
        }

        bool ssrDebugView = pps->GetSSRDebugViewVal();
        if (ImGui::Checkbox("Debug View##ssr", &ssrDebugView))
        {
          pps->SetSSRDebugViewVal(ssrDebugView);
        }

        if (ssrDebugView)
        {
          // Each mode isolates one stage of the pass: an artifact that looks the same in the
          // composite can be told apart here instead of guessed at.
          static const char* ssrDebugModes[] = {
            "Reflection", "Confidence", "Mip Level", "Scene Depth", "Hit UV", "Ray Length", "Hit Error"};

          int ssrDebugMode = pps->GetSSRDebugViewModeVal();
          ImGui::SameLine();
          if (ImGui::Combo("##ssrDebugMode", &ssrDebugMode, ssrDebugModes, IM_ARRAYSIZE(ssrDebugModes)))
          {
            pps->SetSSRDebugViewModeVal(ssrDebugMode);
          }
        }

        ImGui::EndDisabled();
      }

      if (ShowSection("Anti Aliasing"))
      {
        bool fxaaEnabled = pps->GetFXAAEnabledVal();
        if (ImGui::Checkbox("FXAA##1", &fxaaEnabled))
        {
          pps->SetFXAAEnabledVal(fxaaEnabled);
        }
      }

        ImGui::EndChild();
      }

      ImGui::End();
    }

  } // namespace Editor
} // namespace ToolKit
