/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "TopBar.h"

#include "App.h"
#include "ConsoleWindow.h"
#include "EditorCamera.h"
#include "IconsFontAwesome.h"
#include "OutlinerWindow.h"

#include <Dpad.h>
#include <Drawable.h>
#include <GradientSky.h>
#include <Mesh.h>
#include <Surface.h>

namespace ToolKit
{
  namespace Editor
  {

    OverlayTopBar::OverlayTopBar(EditorViewport* owner) : OverlayUI(owner) {}

    void OverlayTopBar::ShowAddMenuPopup()
    {
      EditorScenePtr currScene = GetApp()->GetCurrentScene();
      EntityPtr createdEntity  = nullptr;
      if (ImGui::BeginMenu("Mesh"))
      {
        if (ImGui::MenuItem(ICON_FA_CUBE " Cube"))
        {
          createdEntity = MakeNewPtr<Cube>();
          createdEntity->GetMeshComponent()->Init(false);
        }
        if (ImGui::MenuItem(ICON_FA_CIRCLE " Sphere"))
        {
          createdEntity = MakeNewPtr<Sphere>();
          createdEntity->GetMeshComponent()->Init(false);
        }
        if (ImGui::MenuItem(ICON_FA_CARET_UP " Cone"))
        {
          ConePtr cone = MakeNewPtr<Cone>();
          cone->Generate(1.0f, 1.0f, 30, 30);
          createdEntity = cone;
          createdEntity->GetMeshComponent()->Init(false);
        }
        if (ImGui::MenuItem(ICON_FA_SQUARE " Plane"))
        {
          createdEntity = MakeNewPtr<Quad>();
          createdEntity->GetMeshComponent()->Init(false);
        }
        if (ImGui::MenuItem(ICON_FA_GITHUB_ALT " Monkey"))
        {
          MeshPtr mesh = GetMeshManager()->Create<Mesh>(MeshPath("suzanne.mesh", true));
          mesh->Init(false);
          MeshComponentPtr meshCom = MakeNewPtr<MeshComponent>();
          meshCom->SetMeshVal(mesh);
          EntityPtr suzanne = MakeNewPtr<Entity>();
          suzanne->AddComponent(meshCom);

          createdEntity = suzanne;
        }
        ImGui::EndMenu();
      }
      ImGui::Separator();

      if (ImGui::BeginMenu("2D UI"))
      {
        if (ImGui::MenuItem("Surface"))
        {
          SurfacePtr srf = MakeNewPtr<Surface>();
          srf->SetSizeVal(Vec2(100.0f, 30.0f));
          createdEntity = srf;
          createdEntity->GetMeshComponent()->Init(false);
        }

        if (ImGui::MenuItem("Button"))
        {
          ButtonPtr btn = MakeNewPtr<Button>();
          btn->Update(Vec2(100.0f, 30.0f), Vec2(0.5f, 0.5f));
          createdEntity = btn;
          createdEntity->GetMeshComponent()->Init(false);
        }

        if (ImGui::MenuItem("Dpad"))
        {
          DpadPtr dpad = MakeNewPtr<Dpad>();
          dpad->Update(Vec2(100.0f, 100.0f), Vec2(0.5f, 0.5f));
          createdEntity = dpad;
          createdEntity->GetMeshComponent()->Init(false);
        }

        ImGui::EndMenu();
      }

      ImGui::Separator();
      if (ImGui::MenuItem(ICON_FA_ARROWS " Node"))
      {
        createdEntity = MakeNewPtr<EntityNode>();
      }

      if (ImGui::MenuItem(ICON_FA_VIDEO_CAMERA " Camera"))
      {
        createdEntity = MakeNewPtr<EditorCamera>();
      }

      if (ImGui::BeginMenu(ICON_FA_LIGHTBULB " Light"))
      {
        if (ImGui::MenuItem(ICON_FA_SUN " Directional"))
        {
          EditorDirectionalLightPtr light = MakeNewPtr<EditorDirectionalLight>();
          light->InitController();
          createdEntity = light;
        }

        if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Point"))
        {
          EditorPointLightPtr light = MakeNewPtr<EditorPointLight>();
          light->InitController();
          createdEntity = light;
        }

        if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Spot"))
        {
          EditorSpotLightPtr light = MakeNewPtr<EditorSpotLight>();
          light->InitController();
          createdEntity = light;
        }

        if (ImGui::MenuItem(ICON_FA_CLOUD " Sky"))
        {
          createdEntity = MakeNewPtr<Sky>();
        }

        if (ImGui::MenuItem(ICON_FA_SKYATLAS " Gradient Sky"))
        {
          createdEntity = MakeNewPtr<GradientSky>();
        }

        ImGui::EndMenu();
      }

      // Create dynamic menu.
      ImGui::Separator();
      for (DynamicMenuPtr root : GetApp()->m_customObjectsMenu)
      {
        ShowDynamicMenu(root,
                        [](const StringView& className) -> void
                        {
                          EntityPtr entity = MakeNewPtrCasted<Entity>(className);
                          GetApp()->GetCurrentScene()->AddEntity(entity);
                        });
      }

      if (createdEntity != nullptr)
      {
        const auto isSameTypeFn = [createdEntity](const EntityPtr e) -> bool
        { return e->Class() == createdEntity->Class(); };

        String typeName                = createdEntity->Class()->Name;
        const EntityPtrArray& entities = currScene->GetEntities();
        size_t numSameType             = std::count_if(entities.cbegin(), entities.cend(), isSameTypeFn);

        String suffix                  = numSameType == 0 ? "" : "_" + std::to_string(numSameType);

        // if numSameType equals 0 EntityType otherwise EntityType_123
        createdEntity->SetNameVal(typeName + suffix);

        // Adjust entity location.
        if (GetApp() && GetApp()->m_cursor)
        {
          Vec3 loc = GetApp()->m_cursor->m_worldLocation;
          createdEntity->m_node->SetTranslation(loc);
        }

        currScene->AddEntity(createdEntity);

        if (OutlinerWindowPtr outliner = GetApp()->GetOutliner())
        {
          if (outliner->IsInsertingAtTheEndOfEntities())
          {
            outliner->Focus(createdEntity);
          }

          // if right clicked this will try to insert to where we clicked
          // otherwise(top bar add) this will spawn at the end of the list.
          outliner->TryReorderEntites({createdEntity});
          currScene->ValidateBillboard(createdEntity);
        }
      }
    }

    void OverlayTopBar::Show()
    {
      assert(m_owner);
      if (m_owner == nullptr)
      {
        return;
      }

      ImVec2 overlaySize(360, 30);

      // Center the toolbar.
      float width  = ImGui::GetContentRegionAvail().x;
      float offset = (width - overlaySize.x) * 0.5f;
      ImGui::SameLine(offset);

      ImGui::SetNextWindowBgAlpha(0.65f);
      if (ImGui::BeginChildFrame(ImGui::GetID("ViewportOptions"),
                                 overlaySize,
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse))
      {
        SetOwnerState();

        ImGui::BeginTable("##SettingsBar", 8, ImGuiTableFlags_SizingStretchProp);
        ImGui::TableNextRow();

        uint nextItemIndex = 0;

        ShowAddMenu(ShowAddMenuPopup, nextItemIndex);

        CameraAlignmentOptions(nextItemIndex);

        ShowTransformOrientation(nextItemIndex);

        SnapOptions(nextItemIndex);

        ImGui::EndTable();
      }
      ImGui::EndChildFrame();
    }

    void OverlayTopBar::ShowAddMenu(std::function<void()> showMenuFn, uint& nextItemIndex)
    {
      ImGui::TableSetColumnIndex(nextItemIndex++);

      ImVec2 cursorPos = ImGui::GetCursorPos();
      ImGui::SetCursorPos(ImVec2(cursorPos.x + 3.0f, cursorPos.y + 3.0f));
      ImGui::Text(ICON_FA_GLOBE);
      ImGui::SetCursorPos(cursorPos);

      ImGui::TableSetColumnIndex(nextItemIndex++);

      if (ImGui::Button("Add"))
      {
        ImGui::OpenPopup("##AddMenu");
      }

      // Get button position
      ImVec2 buttonMin = ImGui::GetItemRectMin();
      ImVec2 buttonMax = ImGui::GetItemRectMax();

      ImVec2 popupPos(buttonMin.x, buttonMax.y);

      // Align menu left bottom to make it appear like drop down.
      ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);

      if (ImGui::BeginPopup("##AddMenu"))
      {
        showMenuFn();
        ImGui::EndPopup();
      }
    }

    void OverlayTopBar::ShowTransformOrientation(uint& nextColumnItem)
    {
      bool change = false;
      ImGui::TableSetColumnIndex(nextColumnItem++);
      ImGui::Image(Convert2ImGuiTexture(UI::m_axisIcon), Vec2(20.0f));

      // Transform orientation combo.
      ImGuiStyle& style            = ImGui::GetStyle();
      float spacing                = style.ItemInnerSpacing.x;

      const char* itemsOrient[]    = {"World", "Local"};
      static int currentItemOrient = 0;

      ImGui::TableSetColumnIndex(nextColumnItem++);
      ImGui::PushItemWidth(72);
      if (ImGui::BeginCombo("##TRS", itemsOrient[currentItemOrient], ImGuiComboFlags_None))
      {
        for (int n = 0; n < IM_ARRAYSIZE(itemsOrient); n++)
        {
          bool is_selected = (currentItemOrient == n);
          if (ImGui::Selectable(itemsOrient[n], is_selected))
          {
            if (currentItemOrient != n)
            {
              change = true;
            }
            currentItemOrient = n;
          }

          if (is_selected)
          {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      ImGui::PopItemWidth();

      if (change)
      {
        String ts;
        switch (currentItemOrient)
        {
        case 1:
          ts = "local";
          break;
        case 0:
        default:
          ts = "world";
          break;
        }

        String cmd = "SetTransformOrientation " + ts;
        GetApp()->GetConsole()->ExecCommand(cmd);
      }
      UI::HelpMarker(TKLoc + m_owner->m_name, "Transform orientations\n");
    }

    void OverlayTopBar::SnapOptions(uint& nextItemIndex)
    {
      // Auto snap.
      static bool autoSnapActivated = false;
      if (ImGui::GetIO().KeyCtrl)
      {
        if (!GetApp()->m_snapsEnabled)
        {
          autoSnapActivated        = true;
          GetApp()->m_snapsEnabled = true;
        }
      }
      else if (autoSnapActivated)
      {
        autoSnapActivated        = false;
        GetApp()->m_snapsEnabled = false;
      }

      ImGui::TableSetColumnIndex(nextItemIndex++);
      GetApp()->m_snapsEnabled = UI::ToggleButton(UI::m_snapIcon->m_textureId, Vec2(16.0), GetApp()->m_snapsEnabled);
      UI::HelpMarker(TKLoc + m_owner->m_name, "Grid snaping\nRight click for options");

      if (ImGui::BeginPopupContextItem("##SnapMenu"))
      {
        ImGui::PushItemWidth(75);
        ImGui::InputFloat("Move delta", &m_owner->m_snapDeltas.x, 0.0f, 0.0f, "%.2f");
        ImGui::InputFloat("Rotate delta", &m_owner->m_snapDeltas.y, 0.0f, 0.0f, "%.2f");
        ImGui::InputFloat("Scale delta", &m_owner->m_snapDeltas.z, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::EndPopup();
      }
    }

    void OverlayTopBar::CameraAlignmentOptions(uint& nextItemIndex)
    {
      bool change = false;
      ImGui::TableSetColumnIndex(nextItemIndex++);

      ImGui::Text(ICON_FA_VIDEO_CAMERA);

      ImGui::TableSetColumnIndex(nextItemIndex++);
      m_owner->m_orbitLock =
          UI::ToggleButton(m_owner->m_orbitLock ? ICON_FA_LOCK : ICON_FA_UNLOCK, Vec2(20.0f), m_owner->m_orbitLock);

      UI::HelpMarker(TKLoc + m_owner->m_name,
                     "Lock Camera Alignment\nMiddle button drag doesn't orbit."
                     "\nOnly panning allowed.");

      // Camera alignment combo.
      const char* itemsCam[]   = {"Free", "Top", "Front", "Left", "User"};
      int currentItemCam       = static_cast<int>(m_owner->m_cameraAlignment);
      CameraAlignment rollBack = m_owner->m_cameraAlignment;

      ImGui::TableSetColumnIndex(nextItemIndex++);
      ImGui::PushItemWidth(72);
      if (ImGui::BeginCombo("##VC", itemsCam[currentItemCam], ImGuiComboFlags_None))
      {
        for (int n = 0; n < IM_ARRAYSIZE(itemsCam); n++)
        {
          bool isSelected = (currentItemCam == n);
          if (ImGui::Selectable(itemsCam[n], isSelected))
          {
            if (currentItemCam != n)
            {
              change = true;
            }
            currentItemCam             = n;
            m_owner->m_cameraAlignment = (CameraAlignment) currentItemCam;
          }

          if (isSelected)
          {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      ImGui::PopItemWidth();

      if (change)
      {
        String view;
        switch ((CameraAlignment) currentItemCam)
        {
        case CameraAlignment::Top:
          view = "Top";
          break;
        case CameraAlignment::Front:
          view = "Front";
          break;
        case CameraAlignment::Left:
          view = "Left";
          break;
        case CameraAlignment::User:
          view = "User";
          break;
        case CameraAlignment::Free:
        default:
          view = "Free";
          break;
        }

        if (view == "User")
        {
          bool noCamera = true;
          if (EntityPtr cam = GetApp()->GetCurrentScene()->GetCurrentSelection())
          {
            if (cam->IsA<Camera>())
            {
              if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
              {
                vp->AttachCamera(cam->GetIdVal());
                noCamera = false;
              }
            }
          }

          if (noCamera)
          {
            m_owner->m_cameraAlignment = rollBack;
            GetApp()->SetStatusMsg(g_statusFailed);
            TK_ERR("No camera selected.\nSelect a camera from the scene.");
          }
        }
        else
        {
          if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
          {
            vp->AttachCamera(NullHandle);
          }

          if (view != "Free")
          {
            m_owner->m_cameraAlignment = CameraAlignment::Free;
            String cmd                 = "SetCameraTransform --v \"" + m_owner->m_name + "\" " + view;
            GetApp()->GetConsole()->ExecCommand(cmd);
          }
        }
      }
      UI::HelpMarker(TKLoc + m_owner->m_name, "Camera Orientation\n");
    }

  } // namespace Editor
} // namespace ToolKit