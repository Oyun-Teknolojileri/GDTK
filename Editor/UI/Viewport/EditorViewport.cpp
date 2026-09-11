/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "EditorViewport.h"

#include "App.h"
#include "ConsoleWindow.h"
#include "Grid.h"
#include "LeftBar.h"
#include "Mod.h"
#include "OverlayLighting.h"
#include "PopupWindows.h"
#include "StatusBar.h"
#include "TopBar.h"

#include <Camera.h>
#include <DirectionComponent.h>
#include <Material.h>
#include <MathUtil.h>
#include <Mesh.h>
#include <MeshComponent.h>
#include <Prefab.h>
#include <SDL.h>
#include <Util.h>

namespace ToolKit
{
  namespace Editor
  {

    namespace
    {
      /**
       * Drag and drop state shared by all viewports. ImGui delivers the drag payload
       * globally and the viewport queries it every frame, so the mesh being dragged is
       * cached between frames.
       *
       * The cache is file scope instead of a function local static so that
       * EditorViewport::ReleaseDragDropState() can drop the references it holds. A function
       * local static would keep the dragged entity alive until process exit, which is after
       * ToolKit is gone, and its destructor would then reach into a destroyed engine.
       */
      struct DragDropCache
      {
        LineBatchPtr boundingBox = nullptr;
        EntityPtr draggedMesh    = nullptr;
        bool meshLoaded          = false;
        bool meshAddedToScene    = false;
      };

      DragDropCache& GetDragDropCache()
      {
        static DragDropCache cache;
        return cache;
      }
    } // namespace

    // EditorViewport
    //////////////////////////////////////////

    TKDefineClass(EditorViewport, Window);

    std::vector<OverlayUI*> EditorViewport::m_overlays = {nullptr, nullptr, nullptr, nullptr};

    void InitOverlays(EditorViewport* viewport)
    {
      for (int i = 0; i < 4; i++)
      {
        OverlayUI** overlay = &EditorViewport::m_overlays[i];
        if (*overlay == nullptr)
        {
          switch (i)
          {
            case 0:
              *overlay = new OverlayLeftBar(viewport);
              break;
            case 1:
              *overlay = new OverlayTopBar(viewport);
              break;
            case 2:
              *overlay = new StatusBar(viewport);
              break;
            case 3:
              *overlay = new OverlayLighting(viewport);
              break;
          }
        }
      }
    }

    EditorViewport::EditorViewport()
    {
      m_name = g_viewportStr + " " + std::to_string(m_id);
      Init({640.0f, 480.0f});

      m_editorRenderer = MakeNewPtr<EditorRenderer>();
    }

    EditorViewport::~EditorViewport() {}

    void EditorViewport::Show()
    {
      m_mouseOverOverlay = false;
      ImGui::SetNextWindowSize(Vec2(m_size), ImGuiCond_None);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, g_wndBgColor);

      if (ImGui::Begin(m_name.c_str(),
                       &m_visible,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | m_additionalWindowFlags))
      {
        UpdateContentArea();
        ComitResize();
        UpdateWindow();
        HandleStates();
        HandleDrop();
        DrawOverlays();
        DrawCommands();
        UpdateSnaps();
      }

      ImGui::End();
      ImGui::PopStyleColor();

      // Space opens the command palette over this viewport. It is only
      // triggered when the viewport is interacted with (hovered & active) and
      // the keyboard is not already used for text entry.
      if (!m_commandPaletteOpen && ShouldOpenCommandPalette())
      {
        OpenCommandPalette();
      }

      // The palette is a modal popup that renders on top of the viewport.
      if (m_commandPaletteOpen)
      {
        ShowCommandPalette();
      }
    }

    void EditorViewport::Update(float deltaTime)
    {
      // While the command palette is open the viewport must not react to the
      // camera mods (navigation, zoom) even on the frame it opens.
      if (!IsActive() || m_commandPaletteOpen)
      {
        SDL_GetGlobalMouseState(&m_mousePosBegin.x, &m_mousePosBegin.y);
        return;
      }

      // Update viewport mods.
      bool applyMods = true;
      if (GetApp()->m_gameMod == GameMod::Playing)
      {
        // Apply mods only if the active viewport is not the simulation viewport.
        if (GetApp()->GetActiveViewport() == GetApp()->GetSimulationViewport())
        {
          applyMods = false;
        }
      }

      if (applyMods)
      {
        FpsNavigationMod(deltaTime);
        OrbitPanMod(deltaTime);
      }
    }

    bool EditorViewport::IsViewportQueriable() const
    {
      return m_mouseOverContentArea && m_mouseHover && m_active && m_visible && m_relMouseModBegin;
    }

    void EditorViewport::DispatchSignals() const
    {
      if (!CanDispatchSignals() || m_mouseOverOverlay || m_commandPaletteOpen)
      {
        return;
      }

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_leftMouseBtnDownSgnl);
      }

      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_leftMouseBtnUpSgnl);
      }

      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_leftMouseBtnDragSgnl);
      }

      if (ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_setCursorSgnl);
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
      {
        ModManager::GetInstance()->DispatchSignal(BaseMod::m_delete);
      }

      ModShortCutSignals();
    }

    // Command palette.
    //////////////////////////////////////////

    bool EditorViewport::ShouldOpenCommandPalette() const
    {
      // The viewport must be interacted with (active, hovered, the mouse over
      // its content and not over an overlay).
      if (!CanDispatchSignals() || !m_mouseOverContentArea || m_mouseOverOverlay)
      {
        return false;
      }

      // Do not hijack the space while the keyboard is used for text entry
      // (e.g. rename fields, console input) or while the game simulates.
      if (UI::IsKeyboardCaptured() || GetApp()->m_gameMod != GameMod::Stop)
      {
        return false;
      }

      // Do not interfere with ongoing mouse interactions (camera navigation,
      // dragging assets into the viewport).
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
          ImGui::IsMouseDown(ImGuiMouseButton_Middle))
      {
        return false;
      }

      return ImGui::IsKeyPressed(ImGuiKey_Space, false);
    }

    void EditorViewport::OpenCommandPalette()
    {
      m_commandPaletteOpen         = true;
      m_commandPaletteNeedsFocus   = true;
      m_commandPaletteScrollToSel  = true;
      m_commandPaletteSelection    = 0;
      m_commandPaletteText.clear();
      m_commandPaletteLastToken.clear();

      // Consume the space that opened the palette so it does not leak into the
      // command text as the first character.
      ImGuiIO& io = ImGui::GetIO();
      if (!io.InputQueueCharacters.empty() && io.InputQueueCharacters.back() == ' ')
      {
        io.InputQueueCharacters.pop_back();
      }
    }

    void EditorViewport::CloseCommandPalette()
    {
      if (m_commandPaletteOpen)
      {
        m_commandPaletteOpen = false;
        ImGui::CloseCurrentPopup();

        // Restore the viewport as the active & focused window so the editing
        // flow continues (e.g. space opens the palette again right away).
        m_active = true;
        ImGui::SetWindowFocus(m_name.c_str());

        for (WindowPtr wnd : GetApp()->m_windows)
        {
          if (wnd->GetIdVal() == GetIdVal())
          {
            GetApp()->m_lastActiveViewport = Cast<EditorViewport>(wnd);
            break;
          }
        }
      }
    }

    void EditorViewport::RunCommandPalette()
    {
      if (ConsoleWindowPtr console = GetApp()->GetConsole())
      {
        // Strip the surrounding spaces the completion may have appended.
        String commandLine = m_commandPaletteText;
        while (!commandLine.empty() && commandLine.back() == ' ')
        {
          commandLine.pop_back();
        }

        size_t firstChar = commandLine.find_first_not_of(' ');
        if (firstChar == String::npos)
        {
          firstChar = 0;
        }
        commandLine = commandLine.substr(firstChar);

        if (!commandLine.empty())
        {
          console->ExecCommand(commandLine);
        }
      }

      CloseCommandPalette();
    }

    StringArray EditorViewport::FilterCommandPaletteMatches(const String& token) const
    {
      StringArray matches;
      if (ConsoleWindowPtr console = GetApp()->GetConsole())
      {
        for (const String& command : console->GetCommandList())
        {
          if (token.empty() || Utf8CaseInsensitiveSearch(command, token))
          {
            matches.push_back(command);
          }
        }
      }

      return matches;
    }

    int EditorViewport::CommandPaletteTextCallback(ImGuiInputTextCallbackData* data)
    {
      if (data->EventFlag != ImGuiInputTextFlags_CallbackCompletion)
      {
        return 0;
      }

      // Completion only replaces the command token (the text before the first
      // space). Once the arguments are typed, Tab should not touch the line.
      const char* buf   = data->Buf;
      const char* space = strchr(buf, ' ');
      if (space != nullptr && data->CursorPos > static_cast<int>(space - buf))
      {
        return 0;
      }

      const int tokenLen = (space != nullptr) ? static_cast<int>(space - buf) : data->BufTextLen;
      const String token(buf, tokenLen);
      const StringArray matches = FilterCommandPaletteMatches(token);
      if (matches.empty())
      {
        return 0;
      }

      // Fill in the currently highlighted match (the first one by default) and
      // append a space so the arguments can be typed right away.
      const int sel = glm::clamp(m_commandPaletteSelection, 0, static_cast<int>(matches.size()) - 1);
      data->DeleteChars(0, tokenLen);
      data->InsertChars(0, matches[sel].c_str());
      data->InsertChars(data->CursorPos, " ");

      return 0;
    }

    void EditorViewport::ShowCommandPalette()
    {
      // Keep the palette inside the viewport content area and center it.
      const float margin = 16.0f;
      const float maxW   = glm::max(m_wndContentAreaSize.x - 2.0f * margin, 120.0f);
      const float width  = glm::min(glm::clamp(m_wndContentAreaSize.x * 0.6f, 220.0f, 520.0f), maxW);
      const float centerX = (m_contentAreaMin.x + m_contentAreaMax.x) * 0.5f;
      const float centerY = (m_contentAreaMin.y + m_contentAreaMax.y) * 0.5f;

      ImGuiStyle& style     = ImGui::GetStyle();
      const float rowH      = ImGui::GetFrameHeightWithSpacing();
      const float hintH     = ImGui::GetTextLineHeightWithSpacing();

      // Match count / geometry is evaluated from the last frame's text, the
      // popup is re-sized next frame when the text is edited.
      String token = m_commandPaletteText;
      const size_t spacePos = token.find(' ');
      if (spacePos != String::npos)
      {
        token = token.substr(0, spacePos);
      }
      const StringArray matches = FilterCommandPaletteMatches(token);
      const int matchCount      = static_cast<int>(matches.size());

      const bool hasList     = matchCount > 0;
      const float maxListH   = glm::max(m_wndContentAreaSize.y * 0.45f, rowH);
      const int maxRowsByH   = glm::max(1, static_cast<int>(maxListH / rowH));
      const int visibleRows  = glm::clamp(matchCount, 0, glm::min(8, maxRowsByH));
      const float listH      = static_cast<float>(visibleRows) * rowH;

      float contentH         = rowH; // Command input.
      if (hasList)
      {
        contentH += style.ItemSpacing.y + listH;
      }
      else if (!token.empty())
      {
        contentH += style.ItemSpacing.y + hintH; // "No matching command" row.
      }
      contentH += style.ItemSpacing.y + hintH; // Key legend row.
      const float windowH = contentH + 2.0f * style.WindowPadding.y;

      const String popupId = "##ViewportCommandPalette_" + std::to_string(m_id);

      ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 0));
      ImGui::SetNextWindowPos(ImVec2(centerX, centerY), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(width, windowH), ImGuiCond_Always);
      ImGui::OpenPopup(popupId.c_str());

      if (!ImGui::BeginPopupModal(popupId.c_str(),
                                  nullptr,
                                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoSavedSettings))
      {
        // Popup could not be opened (e.g. closed by another popup). Drop the
        // open state so the palette is not re-opened on the next frame.
        m_commandPaletteOpen = false;
        ImGui::PopStyleColor();
        return;
      }

      {
        if (ImGui::IsWindowAppearing())
        {
          m_commandPaletteNeedsFocus  = true;
          m_commandPaletteScrollToSel = true;
          m_commandPaletteSelection   = 0;
        }

        // Keep the keyboard focus on the command text while the palette is open.
        if (m_commandPaletteNeedsFocus)
        {
          ImGui::SetKeyboardFocusHere();
          m_commandPaletteNeedsFocus = false;
        }

        // Command text. Enter executes, Tab completes the highlighted match
        // through the input callback.
        bool run = false;
        ImGui::PushItemWidth(-1.0f);
        run = ImGui::InputTextWithHint(
            "##CommandPaletteText",
            "Type a command",
            &m_commandPaletteText,
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion,
            [](ImGuiInputTextCallbackData* data) -> int
            { return reinterpret_cast<EditorViewport*>(data->UserData)->CommandPaletteTextCallback(data); },
            this);
        const bool textActive = ImGui::IsItemActive();
        ImGui::PopItemWidth();

        // The list is filtered with the command token: the text before the
        // first space. Arguments do not affect the list.
        if (token != m_commandPaletteLastToken)
        {
          // A text edit invalidates the previous list navigation.
          m_commandPaletteLastToken   = token;
          m_commandPaletteSelection   = 0;
          m_commandPaletteScrollToSel = true;
        }

        if (!matches.empty())
        {
          m_commandPaletteSelection =
              glm::clamp(m_commandPaletteSelection, 0, static_cast<int>(matches.size()) - 1);

          // Arrow keys walk over the matches. The single line text field does
          // not consume them.
          const int count = static_cast<int>(matches.size());
          if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
          {
            m_commandPaletteSelection   = (m_commandPaletteSelection + 1) % count;
            m_commandPaletteScrollToSel = true;
          }

          if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
          {
            m_commandPaletteSelection = (m_commandPaletteSelection - 1 + count) % count;
            m_commandPaletteScrollToSel = true;
          }
        }
        else
        {
          m_commandPaletteSelection = 0;
        }

        if (hasList)
        {
          // Match list. Scrolled when the viewport is too small for all rows.
          if (ImGui::BeginChild("##CommandPaletteList", ImVec2(0.0f, listH), true))
          {
            for (int i = 0; i < matchCount; i++)
            {
              const bool selected = i == m_commandPaletteSelection;
              if (selected && m_commandPaletteScrollToSel)
              {
                ImGui::SetScrollHereY(i == 0 ? 0.0f : 0.5f);
                m_commandPaletteScrollToSel = false;
              }

              const ImVec2 rowSize(ImGui::GetContentRegionAvail().x, 0.0f);
              if (ImGui::Selectable(matches[i].c_str(), selected, ImGuiSelectableFlags_None, rowSize))
              {
                // The clicked row is already visible, no scrolling needed.
                m_commandPaletteSelection = i;
              }
            }
          }
          ImGui::EndChild();
        }
        else if (!token.empty())
        {
          ImGui::TextDisabled("No matching command");
        }

        ImGui::TextDisabled("Up/Down: select, Tab: complete, Enter: run, Esc: close");

        // Regain the text field focus whenever nothing inside the popup is
        // focused anymore (e.g. after clicking on the list).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive())
        {
          m_commandPaletteNeedsFocus = true;
        }

        // Enter runs the command line even when the text field lost the focus.
        if (!run && !textActive &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
        {
          run = true;
        }

        if (run)
        {
          RunCommandPalette();
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
          CloseCommandPalette();
        }
      }

      ImGui::EndPopup();
      ImGui::PopStyleColor();
    }

    XmlNode* EditorViewport::SerializeImp(XmlDocument* doc, XmlNode* parent) const
    {
      XmlNode* wndNode = Super::SerializeImp(doc, parent);
      XmlNode* node    = CreateXmlNode(doc, "Viewport", wndNode);

      WriteAttr(node, doc, "alignment", std::to_string((int) m_cameraAlignment));
      WriteAttr(node, doc, "lock", std::to_string((int) m_orbitLock));
      GetCamera()->Serialize(doc, node);

      return node;
    }

    XmlNode* EditorViewport::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      XmlNode* wndNode      = Super::DeSerializeImp(info, parent);
      XmlNode* viewportNode = wndNode->first_node("Viewport");
      m_wndContentAreaSize  = m_size;

      if (viewportNode)
      {
        ReadAttr(viewportNode, "alignment", *((int*) (&m_cameraAlignment)));
        ReadAttr(viewportNode, "lock", m_orbitLock);

        CameraPtr viewCam  = MakeNewPtr<Camera>();
        viewCam->m_version = m_version;
        ObjectId id        = viewCam->GetIdVal();

        if (m_version > TKV044)
        {
          XmlNode* objNode = viewportNode->first_node(Object::StaticClass()->Name.c_str());
          viewCam->DeSerialize(info, objNode);
        }
        else
        {
          viewCam->DeSerialize(info, viewportNode->first_node("E"));
        }
        viewCam->SetIdVal(id);

        // Reset aspect.
        if (!viewCam->IsOrtographic())
        {
          viewCam->SetLens(glm::quarter_pi<float>(), viewCam->Aspect());
        }

        SetCamera(viewCam);
      }

      return viewportNode;
    }

    void EditorViewport::OnResizeContentArea(float width, float height)
    {
      Viewport::OnResizeContentArea(width, height);
      AdjustZoom(0.0f);
    }

    void EditorViewport::ResizeWindow(uint width, uint height)
    {
      m_size.x      = width;
      m_size.y      = height;
      m_needsResize = true;
    }

    void EditorViewport::GetContentAreaScreenCoordinates(Vec2* min, Vec2* max) const
    {
      *min = m_contentAreaLocation;
      *max = m_contentAreaLocation + m_wndContentAreaSize;
    }

    void EditorViewport::SetCamera(CameraPtr cam)
    {
      Viewport::SetCamera(cam);
      AdjustZoom(0.0f);
    }

    void EditorViewport::ResetCameraToDefault() { SetCamera(MakeNewPtr<Camera>()); }

    void EditorViewport::UpdateContentArea()
    {
      // Content area size

      m_contentAreaMin       = ImGui::GetWindowContentRegionMin();
      m_contentAreaMax       = ImGui::GetWindowContentRegionMax();

      Vec2 wndPos            = Vec2(ImGui::GetWindowPos());
      m_contentAreaMin      += wndPos;
      m_contentAreaMax      += wndPos;

      m_contentAreaLocation  = m_contentAreaMin;

      const Vec2 prevSize    = m_wndContentAreaSize;

      m_wndContentAreaSize   = glm::abs(m_contentAreaMax - m_contentAreaMin);

      if (glm::all(glm::epsilonNotEqual(prevSize, m_wndContentAreaSize, 0.001f)))
      {
        m_needsResize = true;
      }

      ImGuiIO& io            = ImGui::GetIO();
      Vec2 absMousePos       = io.MousePos;

      m_mouseOverContentArea = false;

      if (m_contentAreaMin.x < absMousePos.x && m_contentAreaMax.x > absMousePos.x)
      {
        if (m_contentAreaMin.y < absMousePos.y && m_contentAreaMax.y > absMousePos.y)
        {
          m_mouseOverContentArea = true;
        }
      }

      m_lastMousePosRelContentArea = absMousePos - m_contentAreaMin;
    }

    uint64 EditorViewport::GetImGuiTextureId() const
    {
      // ImGui samples m_renderTarget within the same cb after the render task's FinishPass -
      // within-cb subpass deps handle the layout / write->read transition. Cross-cb hazards
      // are eliminated by FRAMES_IN_FLIGHT=1 in the swapchain.
      TexturePtr texture = m_renderTarget;
      if (texture != nullptr && texture->IsMultiSampled())
      {
        // MSAA: ImGui can only sample the resolved (single-sample) attachment.
        if (TexturePtr resolved = texture->GetResolvedTexture())
        {
          texture = resolved;
        }
        else
        {
          // TODO: We should provide a fallback image ( previous resolved image ) if resolved image is not ready.
          // This would look much better than black screen.
          texture = GetTextureManager()->GetBlackTexture();
        }
      }

      if (texture == nullptr)
      {
        texture = GetTextureManager()->GetBlackTexture();
      }

      return EditorImGuiTextureCache::Acquire(texture);
    }

    void EditorViewport::UpdateWindow()
    {
      if (!ImGui::IsWindowCollapsed())
      {
        // Resize window.
        Vec2 wndSize = ImGui::GetWindowSize();
        if (!VecAllEqual(wndSize, Vec2(m_size)))
        {
          ResizeWindow((uint) wndSize.x, (uint) wndSize.y);
        }

        if (m_wndContentAreaSize.x > 0 && m_wndContentAreaSize.y > 0)
        {
          uint64 texId = GetImGuiTextureId();

          UI::Image(ConvertUIntImGuiTexture(texId), m_wndContentAreaSize);

          if (IsActive())
          {
            ImGui::GetWindowDrawList()->AddRect(m_contentAreaMin, m_contentAreaMax, IM_COL32(255, 255, 0, 255));
          }
          else
          {
            ImGui::GetWindowDrawList()->AddRect(m_contentAreaMin, m_contentAreaMax, IM_COL32(128, 128, 128, 255));
          }
        }
      }

      m_mouseHover = ImGui::IsWindowHovered();
    }

    void EditorViewport::DrawCommands()
    {
      // Process draw commands.
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      for (auto command : m_drawCommands)
      {
        command(drawList);
      }
      m_drawCommands.clear();
    }

    void EditorViewport::FpsNavigationMod(float deltaTime)
    {
      CameraPtr cam = GetCamera();
      if (cam == nullptr)
      {
        return;
      }

      // Allow user camera to fps navigate even in orthographic mod.
      if (m_attachedCamera != NullHandle || !cam->IsOrtographic())
      {
        // Mouse is right clicked
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
          ImGui::SetMouseCursor(ImGuiMouseCursor_None);

          // Adjust camera speed with scroll wheel while right clicking
          if (m_mouseOverContentArea)
          {
            ImGuiIO& io       = ImGui::GetIO();
            float scrollDelta = io.MouseWheel;
            if (glm::notEqual<float>(scrollDelta, 0.0f))
            {
              float& camSpeed        = GetApp()->m_camSpeed;
              // Dynamic speed adjustment: larger values change faster, smaller values change slower
              float speedMultiplier  = camSpeed * 0.1f;
              speedMultiplier        = glm::max(speedMultiplier, 0.05f);
              camSpeed              += scrollDelta * speedMultiplier;
              camSpeed               = glm::clamp(camSpeed, 0.1f, 1000.0f);
            }
          }

          // Handle relative mouse hack.
          if (m_relMouseModBegin)
          {
            m_relMouseModBegin = false;
            SDL_GetGlobalMouseState(&m_mousePosBegin.x, &m_mousePosBegin.y);
          }

          IVec2 absMousePos;
          SDL_GetGlobalMouseState(&absMousePos.x, &absMousePos.y);
          IVec2 delta = absMousePos - m_mousePosBegin;

          SDL_WarpMouseGlobal(m_mousePosBegin.x, m_mousePosBegin.y);
          // End of relative mouse hack.

          if (!VecAllEqual<IVec2>(delta, glm::zero<IVec2>()))
          {
            if (m_cameraAlignment != CameraAlignment::User)
            {
              m_cameraAlignment = CameraAlignment::Free;
            }
          }

          // Apply smoothing factor
          float smoothDeltaX     = delta.x * GetApp()->m_mouseSensitivity;
          float smoothDeltaY     = delta.y * GetApp()->m_mouseSensitivity;

          DirectionComponent* dc = cam->GetComponentFast<DirectionComponent>();
          dc->Pitch(-glm::radians(smoothDeltaY));
          dc->RotateOnUpVector(-glm::radians(smoothDeltaX));

          Vec3 dir, up, right;
          dir         = -Z_AXIS;
          up          = Y_AXIS;
          right       = X_AXIS;

          float speed = GetApp()->m_camSpeed;

          Vec3 move;
          if (ImGui::IsKeyDown(ImGuiKey_A))
          {
            move += -right;
          }

          if (ImGui::IsKeyDown(ImGuiKey_D))
          {
            move += right;
          }

          if (ImGui::IsKeyDown(ImGuiKey_W))
          {
            move += dir;
          }

          if (ImGui::IsKeyDown(ImGuiKey_S))
          {
            move += -dir;
          }

          if (ImGui::IsKeyDown(ImGuiKey_PageUp))
          {
            move += up;
          }

          if (ImGui::IsKeyDown(ImGuiKey_PageDown))
          {
            move += -up;
          }

          float displace = speed * MillisecToSec(deltaTime);
          if (length(move) > 0.0f)
          {
            move = normalize(move);
          }

          cam->m_node->Translate(move * displace, TransformationSpace::TS_LOCAL);
        }
        else
        {
          if (!m_relMouseModBegin)
          {
            m_relMouseModBegin = true;
          }
        }
      }
    }

    void EditorViewport::OrbitPanMod(float deltaTime)
    {
      CameraPtr cam = GetCamera();
      if (cam)
      {
        ImGuiIO& io = ImGui::GetIO();
        if (m_mouseOverContentArea && !io.MouseDown[1])
        {
          // Adjust zoom.
          float delta = io.MouseWheel;
          if (glm::notEqual<float>(delta, 0.0f))
          {
            AdjustZoom(delta);
          }
        }

        static Vec3 orbitPnt;
        static bool hitFound = false;
        static float dist    = 0.0f;
        const Vec3 camPos    = cam->m_node->GetTranslation();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
          // Figure out orbiting point.
          EditorScenePtr currScene = GetApp()->GetCurrentScene();
          EntityPtr currEntity     = currScene->GetCurrentSelection();
          if (currEntity == nullptr)
          {
            if (!hitFound)
            {
              Ray orbitRay             = RayFromMousePosition();
              EditorScene::PickData pd = currScene->PickObject(orbitRay);

              if (pd.entity == nullptr)
              {
                if (!GetApp()->m_grid->HitTest(orbitRay, orbitPnt))
                {
                  orbitPnt = PointOnRay(orbitRay, 5.0f);
                }
              }
              else
              {
                orbitPnt = pd.pickPos;
              }
              hitFound = true;
              dist     = glm::distance(orbitPnt, camPos);
            }
          }
          else
          {
            hitFound = true;
            orbitPnt = currEntity->m_node->GetTranslation(TransformationSpace::TS_WORLD);
            dist     = glm::distance(orbitPnt, camPos);
          }

          // Orbit around it.
          float x = io.MouseDelta.x;
          float y = io.MouseDelta.y;
          Vec3 r  = cam->GetComponent<DirectionComponent>()->GetRight();
          Vec3 u  = cam->GetComponent<DirectionComponent>()->GetUp();

          if (io.KeyShift || m_orbitLock)
          {
            // Reflect window space mouse delta to image plane.
            Vec3 deltaOnImagePlane = glm::unProject(
                // Here, mouse delta is transformed to viewport center.
                Vec3(x + m_wndContentAreaSize.x * 0.5f, y + m_wndContentAreaSize.y * 0.5f, 0.0f),
                Mat4(),
                cam->GetProjectionMatrix(),
                Vec4(0.0f, 0.0f, m_wndContentAreaSize.x, m_wndContentAreaSize.y));

            // Thales ! Reflect imageplane displacement to world space.
            Vec3 deltaOnWorld = deltaOnImagePlane * dist / cam->Near();
            if (cam->IsOrtographic())
            {
              deltaOnWorld = deltaOnImagePlane;
            }

            Vec3 displace = r * -deltaOnWorld.x + u * deltaOnWorld.y;
            cam->m_node->Translate(displace, TransformationSpace::TS_WORLD);
          }
          else
          {
            if (m_cameraAlignment != CameraAlignment::Free)
            {
              if (m_cameraAlignment == CameraAlignment::Top)
              {
                orbitPnt.y = 0.0f;
              }
              else if (m_cameraAlignment == CameraAlignment::Front)
              {
                orbitPnt.z = 0.0f;
              }
              else if (m_cameraAlignment == CameraAlignment::Left)
              {
                orbitPnt.x = 0.0f;
              }
            }

            Mat4 camTs    = cam->m_node->GetTransform(TransformationSpace::TS_WORLD);
            Mat4 ts       = glm::translate(Mat4(), orbitPnt);
            Mat4 its      = glm::translate(Mat4(), -orbitPnt);
            Quaternion qx = glm::angleAxis(-glm::radians(y * GetApp()->m_mouseSensitivity), r);
            Quaternion qy = glm::angleAxis(-glm::radians(x * GetApp()->m_mouseSensitivity), Y_AXIS);

            camTs         = ts * glm::toMat4(qy * qx) * its * camTs;
            cam->m_node->SetTransform(camTs, TransformationSpace::TS_WORLD);

            if (m_cameraAlignment != CameraAlignment::User)
            {
              m_cameraAlignment = CameraAlignment::Free;
            }
          }
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
          hitFound = false;
          dist     = 0.0f;
        }
      }
    }

    void EditorViewport::AdjustZoom(float delta)
    {
      CameraPtr cam = GetCamera();
      cam->m_node->Translate(Vec3(0.0f, 0.0f, -delta), TransformationSpace::TS_LOCAL);

      if (cam->IsOrtographic())
      {
        // Don't allow user camera to have magic zoom.
        if (m_attachedCamera == NullHandle)
        {
          // Magic zoom.
          const Vec3 camPos = cam->m_node->GetTranslation();
          float dist        = glm::distance(ZERO, camPos);
          cam->SetOrthographicScaleVal(dist / 600.0f);
        }
      }
    }

    void EditorViewport::ReleaseDragDropState()
    {
      DragDropCache& cache   = GetDragDropCache();
      cache.boundingBox      = nullptr;
      cache.draggedMesh      = nullptr;
      cache.meshLoaded       = false;
      cache.meshAddedToScene = false;
    }

    void EditorViewport::HandleDrop()
    {
      // Current scene
      EditorScenePtr currScene = GetApp()->GetCurrentScene();

      // Asset drag and drop loading variables
      DragDropCache& cache      = GetDragDropCache();
      LineBatchPtr& boundingBox = cache.boundingBox;
      EntityPtr& dwMesh         = cache.draggedMesh;
      bool& meshLoaded          = cache.meshLoaded;
      bool& meshAddedToScene    = cache.meshAddedToScene;

      // Check if asset drop is activated.
      const ImGuiPayload* dragPayload = ImGui::GetDragDropPayload();
      if (dragPayload && dragPayload->DataSize != sizeof(FileDragData))
      {
        return;
      }

      // AssetBrowser drop handling.
      if (ImGui::BeginDragDropTarget())
      {
        const FileDragData& dragData = FolderView::GetFileDragData();
        if (dragData.Entries == nullptr || dragData.NumFiles <= 0)
        {
          // The payload points into the entries of the view it came from. When that view let them go
          // while the drag was still alive there is nothing to drop, and dereferencing the empty
          // pointer here used to take the editor down with it.
          TK_WRN("Drop ignored: the dragged entries are not available anymore.");
          ImGui::EndDragDropTarget();
          return;
        }

        DirectoryEntry& entry = *dragData.Entries[0]; // get first entry

        // Check if the drag object is a mesh
        Vec3 lastDragMeshPos         = Vec3(0.0f);
        if (entry.m_ext == MESH || entry.m_ext == SKINMESH)
        {
          // Load mesh
          LoadDragMesh(meshLoaded, entry, &dwMesh, &boundingBox, currScene);

          // Show bounding box
          lastDragMeshPos = CalculateDragMeshPosition(meshLoaded, currScene, dwMesh, &boundingBox);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BrowserDragZone"))
        {
          // One line per drop so the console shows which view took it: the asset browsers log their
          // own refusals, and without this a drop that lands here looks like a drop that got lost.
          // The path is relative to the resource root, the full path is mostly the same prefix.
          TK_LOG("Drop on viewport: %s", GetRelativeResourcePath(entry.GetFullPath()).c_str());

          if (entry.m_ext == MESH || entry.m_ext == SKINMESH)
          {
            // Translate mesh to correct position
            dwMesh->m_node->SetTranslation(lastDragMeshPos, TransformationSpace::TS_WORLD);

            if (entry.m_ext == SKINMESH)
            {
              if (dwMesh->GetComponent<AABBOverrideComponent>() == nullptr)
              {
                AABBOverrideComponentPtr aabbOverride = MakeNewPtr<AABBOverrideComponent>();
                aabbOverride->SetBoundingBox(dwMesh->GetBoundingBox());
                dwMesh->AddComponent(aabbOverride);
              }
            }

            // Add mesh to the scene
            currScene->AddEntity(dwMesh);
            currScene->AddToSelection(dwMesh->GetIdVal(), false);
            SetActive();

            meshAddedToScene = true;
          }
          else if (entry.m_ext == SCENE || entry.m_ext == LAYER)
          {
            MultiChoiceButtonInfo openButton;
            openButton.m_name     = "Open";
            openButton.m_callback = [entry]() -> void
            {
              String fullPath = entry.GetFullPath();
              GetApp()->OpenSceneAsync(fullPath);
            };

            MultiChoiceButtonInfo linkButton;
            linkButton.m_name     = "Link";
            linkButton.m_callback = [entry]() -> void
            {
              String fullPath = entry.GetFullPath();
              GetApp()->LinkScene(fullPath);
            };

            MultiChoiceButtonInfo mergeButton;
            mergeButton.m_name     = "Merge";
            mergeButton.m_callback = [entry]() -> void
            {
              String fullPath = entry.GetFullPath();
              GetApp()->MergeScene(fullPath);
            };

            MultiChoiceButtonArray buttons = {openButton, linkButton, mergeButton};
            MultiChoiceWindowPtr importOptionWnd =
                MakeNewPtr<MultiChoiceWindow>("Open Scene", buttons, "Open, link or merge the scene?", true);

            importOptionWnd->AddToUI();
          }
          else if (entry.m_ext == MATERIAL)
          {
            // Find the drop entity
            Ray ray                  = RayFromMousePosition();
            EditorScene::PickData pd = currScene->PickObject(ray);
            if (pd.entity != nullptr && pd.entity->IsDrawable())
            {
              // If there is a mesh component, update material component.
              if (Prefab::GetPrefabRoot(pd.entity) != nullptr)
              {
                GetApp()->SetStatusMsg(g_statusFailed);
                TK_ERR("Failed. Target is Prefab.");
              }
              else
              {
                MeshRawPtrArray meshes;
                if (MeshComponentPtr meshComp = pd.entity->GetComponent<MeshComponent>())
                {
                  if (MeshPtr mesh = meshComp->GetMeshVal())
                  {
                    mesh->GetAllMeshes(meshes);
                  }
                }

                if (meshes.empty())
                {
                  return;
                }

                // Load material once
                String path                = ConcatPaths({entry.m_rootPath, entry.m_fileName + entry.m_ext});
                MaterialPtr material       = GetMaterialManager()->Create<Material>(path);

                // Create a material component if missing one.
                MaterialComponentPtr mmPtr = pd.entity->GetMaterialComponent();
                if (mmPtr == nullptr)
                {
                  GetApp()->SetStatusMsg(g_statusMaterialComponentAdded);
                  mmPtr = pd.entity->AddComponent<MaterialComponent>();
                  mmPtr->UpdateMaterialList();
                }

                // In case of submeshes exist, find sub mesh index.
                if (meshes.size() > 1)
                {
                  float t          = TK_FLT_MAX;
                  uint submeshIndx = FindMeshIntersection(pd.entity, ray, t);
                  if (submeshIndx != TK_UINT_MAX && t != TK_FLT_MAX)
                  {
                    mmPtr->GetMaterialList()[submeshIndx] = material;
                  }
                }
                else
                {
                  mmPtr->SetFirstMaterial(material);
                }
              }
            }
          }
        }

        ImGui::EndDragDropTarget();
      }

      HandleDropMesh(meshLoaded, meshAddedToScene, currScene, &dwMesh, &boundingBox);
    }

    void EditorViewport::DrawOverlays()
    {
      if (GetApp()->m_showOverlayUI)
      {
        if (IsActive() || GetApp()->m_showOverlayUIAlways)
        {
          bool onPlugin = false;
          if (m_name == g_3dViewport && GetApp()->m_gameMod != GameMod::Stop)
          {
            if (!GetApp()->m_simulatorSettings.Windowed)
            {
              // Game is being drawn on 3d viewport. Hide overlays.
              onPlugin = true;
            }
          }

          if (m_name == g_simulationViewStr)
          {
            onPlugin = true;
          }

          if (!onPlugin)
          {
            for (OverlayUI* overlay : m_overlays)
            {
              if (overlay)
              {
                overlay->m_owner = this;
                overlay->Show();
              }
            }
          }
        }
      }
    }

    void EditorViewport::ComitResize()
    {
      if (m_needsResize)
      {
        Vec2 size(m_size);
        Vec2 windowStyleArea = size - m_wndContentAreaSize;
        Vec2 contentAreaSize = size - windowStyleArea;

        if (VecAllEqual(contentAreaSize, Vec2(0.0f)))
        {
          contentAreaSize = size;
        }

        OnResizeContentArea(contentAreaSize.x, contentAreaSize.y);
      }

      m_needsResize = false;
    }

    void EditorViewport::UpdateSnaps()
    {
      if (m_mouseOverContentArea && GetApp()->m_snapsEnabled)
      {
        GetApp()->m_moveDelta   = m_snapDeltas.x;
        GetApp()->m_rotateDelta = m_snapDeltas.y;
        GetApp()->m_scaleDelta  = m_snapDeltas.z;
      }
    }

    void EditorViewport::Init(Vec2 size)
    {
      m_needsResize = true;
      ComitResize();
      InitOverlays(this);
      m_snapDeltas = Vec3(0.25f, 45.0f, 0.25f);
    }

    void EditorViewport::LoadDragMesh(bool& meshLoaded,
                                      DirectoryEntry dragEntry,
                                      EntityPtr* dwMesh,
                                      LineBatchPtr* boundingBox,
                                      EditorScenePtr currScene)
    {
      if (!meshLoaded)
      {
        // Load mesh once
        String path = ConcatPaths({dragEntry.m_rootPath, dragEntry.m_fileName + dragEntry.m_ext});
        *dwMesh     = MakeNewPtr<Entity>();
        (*dwMesh)->AddComponent<MeshComponent>();

        MeshPtr mesh;
        if (dragEntry.m_ext == SKINMESH)
        {
          mesh = GetMeshManager()->Create<SkinMesh>(path);
        }
        else
        {
          mesh = GetMeshManager()->Create<Mesh>(path);
        }

        (*dwMesh)->GetMeshComponent()->SetMeshVal(mesh);
        mesh->Init(false);

        if (mesh->IsSkinned())
        {
          SkeletonComponentPtr skelComp = (*dwMesh)->AddComponent<SkeletonComponent>();
          skelComp->SetSkeletonResourceVal(((SkinMesh*) mesh.get())->m_skeleton);

          skelComp->Init();
        }

        MaterialComponentPtr matComp = (*dwMesh)->AddComponent<MaterialComponent>();
        matComp->UpdateMaterialList();

        // Load bounding box once
        *boundingBox = CreateBoundingBoxDebugObject((*dwMesh)->GetBoundingBox(true));

        // Add bounding box to the scene
        currScene->AddEntity(*boundingBox);

        meshLoaded = true;
      }
    }

    Vec3 EditorViewport::CalculateDragMeshPosition(bool& meshLoaded,
                                                   EditorScenePtr currScene,
                                                   EntityPtr dwMesh,
                                                   LineBatchPtr* boundingBox)
    {
      Vec3 lastDragMeshPos = Vec3(0.0f);
      Ray ray              = RayFromMousePosition(); // Find the point of the cursor in 3D coordinates

      IDArray ignoreList;
      if (meshLoaded)
      {
        ignoreList.push_back((*boundingBox)->GetIdVal());
      }

      EditorScene::PickData pd = currScene->PickObject(ray, ignoreList);
      bool meshFound           = false;
      if (pd.entity != nullptr)
      {
        meshFound       = true;
        lastDragMeshPos = pd.pickPos;
      }
      else
      {
        // Locate the mesh to grid
        lastDragMeshPos = PointOnRay(ray, 5.0f);
        GetApp()->m_grid->HitTest(ray, lastDragMeshPos);
      }

      // Change drop mode with space key
      static bool boxMode = false;
      if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
      {
        boxMode = !boxMode;
      }

      if (meshFound && boxMode)
      {
        float firstY       = lastDragMeshPos.y;
        lastDragMeshPos.y -= dwMesh->GetBoundingBox(false).min.y;

        if (firstY > lastDragMeshPos.y)
        {
          lastDragMeshPos.y = firstY;
        }
      }

      (*boundingBox)->m_node->SetTranslation(lastDragMeshPos, TransformationSpace::TS_WORLD);

      return lastDragMeshPos;
    }

    void EditorViewport::HandleDropMesh(bool& meshLoaded,
                                        bool& meshAddedToScene,
                                        EditorScenePtr currScene,
                                        EntityPtr* dwMesh,
                                        LineBatchPtr* boundingBox)
    {
      if (meshLoaded && !ImGui::IsMouseDragging(0))
      {
        // Remove debug bounding box mesh from scene
        currScene->RemoveEntity((*boundingBox)->GetIdVal());
        meshLoaded = false;

        // The drag session is over either way: when the mesh made it into the scene the scene
        // owns it now, otherwise it is discarded. Keeping a reference here would leak the
        // entity past the lifetime of the engine.
        *dwMesh          = nullptr;
        meshAddedToScene = false;

        // Unload bounding box mesh
        *boundingBox = nullptr;
      }
    }

  } // namespace Editor
} // namespace ToolKit
