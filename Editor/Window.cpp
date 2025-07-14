/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Window.h"

#include "Action.h"
#include "App.h"
#include "EditorViewport.h"
#include "MathUtil.h"
#include "Mod.h"
#include "OutlinerWindow.h"
#include "SimulationWindow.h"

namespace ToolKit
{
  namespace Editor
  {

    TKDefineAbstractClass(Window, Object);

    uint Window::m_baseId = 0;

    Window::Window()
    {
      m_size = UVec2(640, 480);
      m_id   = m_baseId++;
    }

    Window::~Window() {}

    void Window::SetVisibility(bool visible) { m_visible = visible; }

    bool Window::IsShown() { return m_isShown; }

    bool Window::IsActive() const { return m_active; }

    bool Window::IsVisible() const { return m_visible; }

    bool Window::IsMoving() const { return m_moving; }

    bool Window::MouseHovers() const { return m_mouseHover; }

    bool Window::CanDispatchSignals() const { return m_active && m_visible && m_mouseHover; }

    void Window::DispatchSignals() const {}

    void Window::AddToUI()
    {
      m_visible = true;
      if (GetApp() != nullptr)
      {
        UI::m_volatileWindows.push_back(Self<Window>());
      }
    }

    void Window::RemoveFromUI()
    {
      m_visible = false;
      if (GetApp() != nullptr)
      {
        Object* self = this;
        erase_if(UI::m_volatileWindows, [self](WindowPtr wnd) -> bool { return wnd->IsSame(self); });
      }
    }

    void Window::ResetState() { m_isShown = false; }

    XmlNode* Window::SerializeImp(XmlDocument* doc, XmlNode* parent) const
    {
      XmlNode* wndNode = Super::SerializeImp(doc, parent);

      XmlNode* node    = CreateXmlNode(doc, "Window", wndNode);
      WriteAttr(node, doc, XmlVersion, TKVersionStr);

      WriteAttr(node, doc, XmlNodeName.data(), m_name);
      WriteAttr(node, doc, "id", std::to_string(m_id));
      WriteAttr(node, doc, "visible", std::to_string((int) m_visible));
      WriteAttr(node, doc, "shown", std::to_string((int) m_isShown));

      XmlNode* childNode = CreateXmlNode(doc, "Size", node);
      WriteVec(childNode, doc, m_size);

      childNode = CreateXmlNode(doc, "Location", node);
      WriteVec(childNode, doc, m_location);

      return node;
    }

    XmlNode* Window::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      parent           = Super::DeSerializeImp(info, parent);
      XmlNode* wndNode = parent->first_node("Window");

      if (wndNode == nullptr)
      {
        assert(wndNode && "Can't find Window node in document.");
        return nullptr;
      }

      // if not present, version must be v0.4.4
      ReadAttr(wndNode, XmlVersion.data(), m_version, TKV044);
      ReadAttr(wndNode, XmlNodeName.data(), m_name);
      ReadAttr(wndNode, "id", m_id);

      // Type is determined by the corresponding constructor.
      ReadAttr(wndNode, "visible", m_visible);
      ReadAttr(wndNode, "shown", _hadFocus); // Read the focus on one time use variable.

      if (XmlNode* childNode = wndNode->first_node("Size"))
      {
        ReadVec(childNode, m_size);
      }

      if (XmlNode* childNode = wndNode->first_node("Location"))
      {
        ReadVec(childNode, m_location);
      }

      return wndNode;
    }

    void Window::HandleStates()
    {
      ImGui::GetIO().WantCaptureMouse = true;

      // Update moving status.
      Vec2 loc                        = ImGui::GetWindowPos();
      IVec2 iLoc(loc);

      if (m_moving)
      {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
          m_moving = false;
        }
      }
      else
      {
        if (VecAllEqual(iLoc, m_location))
        {
          m_moving = false;
        }
        else
        {
          m_moving = true;
        }
      }

      m_location     = iLoc;

      int hoverFlags = ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup;
      m_mouseHover   = ImGui::IsWindowHovered(hoverFlags);

      TryActivateWindow();

      // If its visible and we are inside ImGui::Begin / End, than window is being shown.
      // (not in a hidden dock or collapsed. )
      m_isShown = true;
    }

    void Window::SetActive()
    {
      m_active = true;
      ImGui::SetWindowFocus();

      if (IsA<EditorViewport>())
      {
        // get the self ref.
        for (WindowPtr wnd : GetApp()->m_windows)
        {
          if (wnd->GetIdVal() == GetIdVal())
          {
            GetApp()->m_lastActiveViewport = Cast<EditorViewport>(wnd);
          }
        }
      }
    }

    void Window::TryActivateWindow()
    {
      bool rightClick  = ImGui::IsMouseDown(ImGuiMouseButton_Right);
      bool leftClick   = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      bool middleClick = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

      // Activate with any click.
      if ((rightClick || leftClick || middleClick) && m_mouseHover)
      {
        bool mouseDrag  = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        mouseDrag      |= ImGui::IsMouseDragging(ImGuiMouseButton_Right);
        mouseDrag      |= ImGui::IsMouseDragging(ImGuiMouseButton_Middle);

        if (mouseDrag)
        {
          // Prevent activation if mouse is dragging.
          return;
        }

        if (!m_active)
        {
          SetActive();
        }
      }

      if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
      {
        if (m_active)
        {
          m_active = false;
        }
      }
    }

    void Window::ModShortCutSignals(const IntArray& mask) const
    {
      if (!CanDispatchSignals() || UI::IsKeyboardCaptured())
      {
        return;
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !Exist(mask, ImGuiKey_Delete))
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_delete);
      }

      if ((ImGui::IsKeyDown(ImGuiKey_ModCtrl) || ImGui::IsKeyDown(ImGuiKey_ModShift)) &&
          ImGui::IsKeyPressed(ImGuiKey_D, false) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
          !Exist(mask, ImGuiKey_D))
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_duplicate);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_C, false) && !Exist(mask, ImGuiKey_C))
      {
        ModManager::GetInstance()->SetMod(true, ModId::Cursor);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_B, false) && !Exist(mask, ImGuiKey_B))
      {
        ModManager::GetInstance()->SetMod(true, ModId::Select);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
          !Exist(mask, ImGuiKey_S))
      {
        ModManager::GetInstance()->SetMod(true, ModId::Scale);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !Exist(mask, ImGuiKey_R))
      {
        ModManager::GetInstance()->SetMod(true, ModId::Rotate);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_G, false) && !Exist(mask, ImGuiKey_G))
      {
        ModManager::GetInstance()->SetMod(true, ModId::Move);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !Exist(mask, ImGuiKey_F))
      {
        if (EntityPtr ntt = GetApp()->GetCurrentScene()->GetCurrentSelection())
        {
          if (OutlinerWindowPtr outliner = GetApp()->GetOutliner())
          {
            outliner->Focus(ntt);
          }
          // Focus the object in the scene
          GetApp()->FocusEntity(ntt);
        }
      }

      // Undo - Redo.
      if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && !Exist(mask, ImGuiKey_Z))
      {
        if (ImGui::IsKeyDown(ImGuiKey_ModCtrl))
        {
          if (ImGui::IsKeyDown(ImGuiKey_ModShift))
          {
            ActionManager::GetInstance()->Redo();
          }
          else
          {
            ActionManager::GetInstance()->Undo();
          }
        }
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      {
        GetApp()->GetCurrentScene()->ClearSelection();
      }

      if (ImGui::IsKeyDown(ImGuiKey_ModCtrl) && ImGui::IsKeyPressed(ImGuiKey_S, false))
      {
        GetApp()->GetCurrentScene()->ClearSelection();
        GetApp()->OnSaveScene();
      }

      if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
      {
        if (GetApp()->m_gameMod == GameMod::Playing || GetApp()->m_gameMod == GameMod::Paused)
        {
          GetApp()->SetGameMod(GameMod::Stop);
        }
        else
        {
          GetApp()->SetGameMod(GameMod::Playing);
        }
      }
    }

  } // namespace Editor
} // namespace ToolKit