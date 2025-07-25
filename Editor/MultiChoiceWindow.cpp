/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "MultiChoiceWindow.h"

#include "App.h"
#include "CustomDataView.h"

namespace ToolKit::Editor
{
  TKDefineClass(MultiChoiceCraeteWindow, Window);

  MultiChoiceCraeteWindow::MultiChoiceCraeteWindow() { m_name = "MultiChoice Parameter Create Window"; }

  bool MultiChoiceCraeteWindow::IsVariantValid()
  {
    if (m_variant.Choices.size() < 2ull)
    {
      GetApp()->SetStatusMsg(g_statusFailed);

      TK_ERR("You must define at least two parameter.");
      return false;
    }

    for (const ParameterVariant& var : m_variant.Choices)
    {
      if (var.m_name.size() < 1)
      {
        GetApp()->SetStatusMsg(g_statusFailed);
        TK_ERR("Name can't be empty.");
        return false;
      }
    }

    return true;
  }

  void MultiChoiceCraeteWindow::ShowVariant()
  {
    CustomDataView::BeginShowVariants("New Variant");
    int removeIndex = -1;

    // draw&edit each parameter
    for (int i = 0; i < (int) m_variant.Choices.size(); i++)
    {
      ParameterVariant* var = &m_variant.Choices[i];

      bool remove           = false;
      CustomDataView::ShowVariant(var, remove, i, true);
      if (remove)
      {
        removeIndex = i;
      }
    }

    if (removeIndex != -1)
    {
      m_variant.Choices.erase(m_variant.Choices.begin() + removeIndex);
    }

    CustomDataView::EndShowVariants();

    int dataType = 0;
    if (ImGui::Combo("AddChoice",
                     &dataType,
                     "Select Type"
                     "\0String\0Boolean\0Int\0Float\0Vec2\0Vec3\0Vec4\0Mat3\0Mat4"))
    {
      switch (dataType)
      {
      case 0:
      case 1:
        m_variant.Choices.push_back(ParameterVariant(String("")));
        break;
      case 2:
        m_variant.Choices.push_back(ParameterVariant(false));
        break;
      case 3:
        m_variant.Choices.push_back(ParameterVariant(0));
        break;
      case 4:
        m_variant.Choices.push_back(ParameterVariant(0.0f));
        break;
      case 5:
        m_variant.Choices.push_back(ParameterVariant(Vec2()));
        break;
      case 6:
        m_variant.Choices.push_back(ParameterVariant(Vec3()));
        break;
      case 7:
        m_variant.Choices.push_back(ParameterVariant(Vec4()));
        break;
      case 8:
        m_variant.Choices.push_back(ParameterVariant(Mat3()));
        break;
      case 9:
        m_variant.Choices.push_back(ParameterVariant(Mat4()));
        break;
      default:
        assert(false && "parameter type invalid");
        break;
      }
    }
  }

  void MultiChoiceCraeteWindow::Show()
  {
    ImGuiIO io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(400, 250), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Once,
                            ImVec2(0.5f, 0.5f));

    ImGui::Begin(m_name.c_str(), nullptr);

    ShowVariant();

    if (ImGui::Button("Create"))
    {
      if (!IsVariantValid())
      {
        ImGui::End();
        return;
      }

      ParameterVariant customVar;
      // This makes them only visible in Custom Data dropdown.
      customVar.m_exposed  = true;
      customVar.m_editable = true;
      customVar.m_category = CustomDataCategory;
      customVar            = m_variant;

      m_parameter->Add(customVar);
      m_menuOpen = false;

      AddToUI();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      m_menuOpen = false;
      RemoveFromUI();
    }

    ImGui::End();
  }

  void MultiChoiceCraeteWindow::OpenCreateWindow(ParameterBlock* parameter)
  {
    if (m_menuOpen)
    {
      return;
    }

    AddToUI();

    m_variant   = MultiChoiceVariant();
    m_menuOpen  = true;
    m_parameter = parameter;
  }
} // namespace ToolKit::Editor