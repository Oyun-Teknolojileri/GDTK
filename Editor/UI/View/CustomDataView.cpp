/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "CustomDataView.h"

#include "App.h"
#include "ComponentView.h"
#include "DopeSheetView.h"
#include "EditorScene.h"
#include "MultiChoiceWindow.h"

#include <Material.h>
#include <Mesh.h>
#include <Prefab.h>

#include <variant>

namespace ToolKit
{
  namespace Editor
  {

    namespace
    {
      // Owner prefixes of the rows being drawn. The inspector pushes one per block of rows (entity,
      // component, material slot) and the key diamond appends the parameter name to it, which is how a
      // track id is built without every call site passing its owner through.
      StringArray& GetKeyOwnerStack()
      {
        static StringArray stack;
        return stack;
      }
    } // namespace

    void CustomDataView::PushKeyOwner(const String& owner)
    {
      if (owner.empty())
      {
        return;
      }

      GetKeyOwnerStack().push_back(owner);
    }

    void CustomDataView::PopKeyOwner()
    {
      StringArray& stack = GetKeyOwnerStack();
      if (!stack.empty())
      {
        stack.pop_back();
      }
    }

    void CustomDataView::ShowKeyDiamond(ParameterVariant* var)
    {
      // The diamond anchors the row it belongs to: it is the first item on the line and every caller
      // follows it with SameLine() before the widget it keys. A row that draws no diamond still has to
      // occupy that slot, otherwise the SameLine() of several rows hangs them off the item before them
      // and the whole block collapses onto one line.
      const float size = ImGui::GetFrameHeight();
      auto reserveSlot = [size]() -> void { ImGui::Dummy(Vec2(size, size)); };

      StringArray& stack = GetKeyOwnerStack();
      if (var == nullptr || stack.empty())
      {
        // No owner: the row can not be addressed by a track id, so there is nothing to key.
        reserveSlot();
        return;
      }

      const String trackId = stack.back() + "." + var->m_name;

      DopeSheetWindowPtr sheet = GetApp()->GetDopeSheet();
      if (sheet == nullptr)
      {
        reserveSlot();
        return;
      }

      const DopeSheetView::ParamKeyState state = sheet->GetParamKeyState(trackId);
      if (state == DopeSheetView::ParamKeyState::NoClip)
      {
        // Without a clip there is no key state to show, and the sheet is where a clip is bound. The
        // slot stays, the diamond does not.
        reserveSlot();
        return;
      }

      const ImVec2 pos = ImGui::GetCursorScreenPos();

      // The track id is unique, a parameter name is not: two rows named "Color" (an entity color and
      // a material color) can sit in the same window.
      ImGui::PushID(trackId.c_str());
      ImGui::InvisibleButton("##keyDiamond", Vec2(size, size));
      const bool clicked      = ImGui::IsItemClicked(ImGuiMouseButton_Left);
      const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
      const bool hovered      = ImGui::IsItemHovered();

      // The diamond mirrors the key markers of the sheet, so a key reads the same in both places.
      const ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
      const float radius  = glm::max(3.0f, size * 0.26f);
      const ImU32 filled  = ImGui::GetColorU32(ImVec4(g_selectHighLightPrimaryColor));
      const ImU32 dim     = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const ImU32 outline = ImGui::GetColorU32(ImGuiCol_Text);
      ImDrawList* dl      = ImGui::GetWindowDrawList();

      const ImVec2 top(center.x, center.y - radius);
      const ImVec2 right(center.x + radius, center.y);
      const ImVec2 bottom(center.x, center.y + radius);
      const ImVec2 left(center.x - radius, center.y);

      if (state == DopeSheetView::ParamKeyState::KeyAtFrame)
      {
        dl->AddQuadFilled(top, right, bottom, left, filled);
        dl->AddQuad(top, right, bottom, left, outline, 1.5f);
      }
      else if (state == DopeSheetView::ParamKeyState::Animated)
      {
        dl->AddQuadFilled(top, right, bottom, left, filled);
      }
      else
      {
        dl->AddQuad(top, right, bottom, left, dim, 1.5f);
      }

      if (hovered)
      {
        const char* stateText = state == DopeSheetView::ParamKeyState::KeyAtFrame ? "key on this frame"
                                : state == DopeSheetView::ParamKeyState::Animated     ? "animated"
                                                                                      : "not animated";
        ImGui::SetTooltip("%s\n%s (frame %d)\nClick: set key   Right click: menu",
                          trackId.c_str(),
                          stateText,
                          sheet->GetFrame());
      }

      if (rightClicked || (clicked && state == DopeSheetView::ParamKeyState::KeyAtFrame))
      {
        ImGui::OpenPopup("##keyDiamondCtx");
      }

      if (clicked && state != DopeSheetView::ParamKeyState::KeyAtFrame)
      {
        sheet->SetParamKey(trackId);
      }

      if (ImGui::BeginPopup("##keyDiamondCtx"))
      {
        if (ImGui::MenuItem("Set Key"))
        {
          sheet->SetParamKey(trackId);
        }

        const bool hasKeyHere = sheet->GetParamKeyState(trackId) == DopeSheetView::ParamKeyState::KeyAtFrame;
        if (ImGui::MenuItem("Delete Key", nullptr, false, hasKeyHere))
        {
          sheet->DeleteParamKey(trackId, sheet->GetFrame());
        }

        ImGui::EndPopup();
      }

      ImGui::PopID();
    }

    void CustomDataView::ShowMaterialPtr(const String& uniqueName,
                                         const String& file,
                                         MaterialPtr& var,
                                         bool isEditable)
    {
      DropSubZone(
          uniqueName,
          EditorImGuiTextureCache::Acquire(UI::m_materialIcon),
          file,
          [&var](const DirectoryEntry& entry) -> void
          {
            if (GetResourceType(entry.m_ext) == Material::StaticClass())
            {
              var = GetMaterialManager()->Create<Material>(entry.GetFullPath());
            }
            else
            {
              TK_ERR("Only Material is accepted.");
            }
          },
          isEditable);
    }

    void CustomDataView::ShowMaterialVariant(const String& uniqueName, const String& file, ParameterVariant* var)
    {
      DropSubZone(
          uniqueName,
          EditorImGuiTextureCache::Acquire(UI::m_materialIcon),
          file,
          [&var](const DirectoryEntry& entry) -> void
          {
            if (GetResourceType(entry.m_ext) == Material::StaticClass())
            {
              *var = GetMaterialManager()->Create<Material>(entry.GetFullPath());
            }
            else
            {
              TK_ERR("Only Material is accepted.");
            }
          },
          var->m_editable);
    }

    ValueUpdateFn CustomDataView::MultiUpdate(ParameterVariant* var, ClassMeta* componentClass)
    {
      EntityPtrArray entities;
      GetApp()->GetCurrentScene()->GetSelectedEntities(entities);

      // Remove current selected because its already updated.
      entities.pop_back();

      ValueUpdateFn multiUpdate = [var, entities, componentClass](Value& oldVal, Value& newVal) -> void
      {
        for (EntityPtr ntt : entities)
        {
          // If entity is a prefab scene entity, skip it
          PrefabPtr prefabRoot = Prefab::GetPrefabRoot(ntt);
          if (prefabRoot && prefabRoot != ntt)
          {
            continue;
          }

          ParameterBlock* paramBlock = nullptr;
          if (componentClass)
          {
            // Perform on the component.
            if (ComponentPtr com = ntt->GetComponent(componentClass))
            {
              paramBlock = &com->m_localData;
            }
          }
          else
          {
            paramBlock = &ntt->m_localData;
          }

          // Perform on the entity.
          ParameterVariant* vLookUp = nullptr;
          if (paramBlock->LookUp(var->m_category.Name, var->m_name, &vLookUp))
          {
            // Assign through the variant's typed setters instead of SetValue(): SetValue() writes the
            // variant storage directly and skips m_onValueChangedFn, so the other entities of the
            // selection kept a stale cache (a light kept its old color on the GPU).
            std::visit([vLookUp](auto&& value) -> void { *vLookUp = value; }, newVal);
          }
        }
      };

      return multiUpdate;
    }

    bool CustomDataView::BeginShowVariants(StringView header)
    {
      if (!ImGui::BeginTable(header.data(), 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedSame))
      {
        return false;
      }
      Vec2 xSize  = ImGui::CalcTextSize("Name");
      xSize      *= 3.0f;
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, xSize.x);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

      xSize  = ImGui::CalcTextSize(ICON_FA_MINUS);
      xSize *= 2.5f;
      ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, xSize.x);

      ImGui::TableHeadersRow();

      ImGui::TableSetColumnIndex(0);
      ImGui::PushItemWidth(-FLT_MIN);

      ImGui::TableSetColumnIndex(1);
      ImGui::PushItemWidth(-FLT_MIN);
      return true;
    }

    void CustomDataView::ShowVariant(ParameterVariant* var, bool& remove, int uiId, bool isListEditable)
    {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);

      ImGui::PushID((int) uiId);
      static char buff[1024];
      strncpy(buff, var->m_name.c_str(), sizeof(buff) - 1);
      buff[sizeof(buff) - 1] = '\0';

      String pNameId         = "##Name" + std::to_string(uiId);
      if (isListEditable)
      {
        ImGui::InputText(pNameId.c_str(), buff, sizeof(buff));
      }
      else
      {
        ImGui::Text(var->m_name.c_str());
      }
      var->m_name = buff;

      ImGui::TableSetColumnIndex(1);

      String pId = "##" + std::to_string(uiId);
      switch (var->GetType())
      {
        case ParameterVariant::VariantType::String:
        {
          ImGui::InputText(pId.c_str(), var->GetVarPtr<String>());
        }
        break;
        case ParameterVariant::VariantType::Bool:
        {
          bool val = var->GetVar<bool>();
          if (ImGui::Checkbox(pId.c_str(), &val))
          {
            *var = val;
          }
        }
        break;
        case ParameterVariant::VariantType::Int:
        {
          ImGui::InputInt(pId.c_str(), var->GetVarPtr<int>());
        }
        break;
        case ParameterVariant::VariantType::Float:
        {
          ImGui::DragFloat(pId.c_str(), var->GetVarPtr<float>(), 0.1f);
        }
        break;
        case ParameterVariant::VariantType::Vec3:
        {
          ImGui::DragFloat3(pId.c_str(), &var->GetVar<Vec3>()[0], 0.1f);
        }
        break;
        case ParameterVariant::VariantType::Vec4:
        {
          ImGui::DragFloat4(pId.c_str(), &var->GetVar<Vec4>()[0], 0.1f);
        }
        break;
        case ParameterVariant::VariantType::Mat3:
        {
          Vec3 vec;
          Mat3 val = var->GetVar<Mat3>();
          for (int j = 0; j < 3; j++)
          {
            pId += std::to_string(j);
            vec  = glm::row(val, j);
            ImGui::InputFloat3(pId.c_str(), &vec[0]);
            val  = glm::row(val, j, vec);
            *var = val;
          }
        }
        break;
        case ParameterVariant::VariantType::Mat4:
        {
          Vec4 vec;
          Mat4 val = var->GetVar<Mat4>();
          for (int j = 0; j < 4; j++)
          {
            pId += std::to_string(j);
            vec  = glm::row(val, j);
            ImGui::InputFloat4(pId.c_str(), &vec[0]);
            val  = glm::row(val, j, vec);
            *var = val;
          }
        }
        break;
        case ParameterVariant::VariantType::MultiChoice:
        {
          MultiChoiceVariant* mcv = var->GetVarPtr<MultiChoiceVariant>();
          String name             = var->m_name + "##MultiChoiceVariant";
          if (ImGui::BeginCombo(name.c_str(), mcv->Choices[mcv->CurrentVal.Index].m_name.c_str()))
          {
            for (uint i = 0; i < mcv->Choices.size(); i++)
            {
              bool isSelected = i == mcv->CurrentVal.Index;
              if (ImGui::Selectable(mcv->Choices[i].m_name.c_str(), isSelected))
              {
                mcv->CurrentVal = {i};
              }
            }
            ImGui::EndCombo();
          }
        }
        break;
      }

      ImGui::TableSetColumnIndex(2);

      remove = isListEditable && ImGui::Button(ICON_FA_MINUS);

      ImGui::PopID();
    }

    void CustomDataView::EndShowVariants()
    {
      ImGui::EndTable();
      ImGui::Separator();
    }

    void CustomDataView::ShowCustomData(EntityPtr entity,
                                        const String& headerName,
                                        const IntArray& vars,
                                        bool isListEditable)
    {
      if (headerName.length() && !ImGui::CollapsingHeader(headerName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
      {
        return;
      }

      if (BeginShowVariants(headerName))
      {
        int removeIndex  = -1;
        int displayIndex = -1;
        for (int i = 0; i < (int) vars.size(); i++)
        {
          int index                   = vars[i];
          ParameterVariant* var       = &entity->m_localData[index];
          ValueUpdateFn multiUpdateFn = MultiUpdate(var);
          var->m_onValueChangedFn.push_back(multiUpdateFn);

          bool remove = false;
          ShowVariant(var, remove, i, isListEditable);
          if (remove)
          {
            removeIndex  = index;
            displayIndex = i;
          }

          var->m_onValueChangedFn.pop_back();
        }

        if (removeIndex != -1)
        {
          ParameterVariant* var = &entity->m_localData[removeIndex];
          GetApp()->SetStatusMsg(Format("Parameter %d: %s removed.", displayIndex + 1, var->m_name.c_str()));
          entity->m_localData.Remove(removeIndex);
        }
      }
      EndShowVariants();

      static bool addInAction = false;
      if (isListEditable && addInAction)
      {
        ImGui::PushItemWidth(150);
        int dataType = 0;
        if (ImGui::Combo("##NewCustData",
                         &dataType,
                         "Select"
                         "Type\0String\0Boolean\0Int\0Float\0Vec2\0Vec3\0Vec4"
                         "\0Mat3\0Mat4\0MultiChoice"))
        {
          ParameterVariant customVar;
          // This makes them only visible in Custom Data dropdown.
          customVar.m_exposed  = true;
          customVar.m_editable = true;
          customVar.m_category = CustomDataCategory;

          bool added           = true;
          switch (dataType)
          {
            case 0:
              added = false;
              break;
            case 1:
              customVar = "";
              break;
            case 2:
              customVar = false;
              break;
            case 3:
              customVar = 0;
              break;
            case 4:
              customVar = 0.0f;
              break;
            case 5:
              customVar = Vec2(0.0f, 0.0f);
            case 6:
              customVar = ZERO;
              break;
            case 7:
              customVar = Vec4();
              break;
            case 8:
              customVar = Mat3();
              break;
            case 9:
              customVar = Mat4();
              break;
            case 10:
            {
              MultiChoiceCraeteWindowPtr multiParamWnd = MakeNewPtr<MultiChoiceCraeteWindow>();
              multiParamWnd->OpenCreateWindow(&entity->m_localData);
              multiParamWnd->AddToUI();

              added       = false;
              addInAction = false;
            }
            break;
            default:
              assert(false && "invalid data type");
              break;
          }

          if (added)
          {
            entity->m_localData.Add(customVar);
            addInAction = false;
          }
        }
        ImGui::PopItemWidth();
      }

      if (isListEditable)
      {
        if (UI::BeginCenteredTextButton("Add Custom Data"))
        {
          addInAction = true;
        }
        UI::EndCenteredTextButton();
      }
    }

    void CustomDataView::ShowVariant(ParameterVariant* var, ComponentPtr comp, ValueUpdateFn callback)
    {
      if (!var->m_exposed)
      {
        return;
      }

      // The animation records table manages its own per cell edit gating, so
      // the play/pause/stop preview buttons stay usable in read only (e.g.
      // prefab) mode while the cells remain locked.
      const bool isAnimRecords = (var->GetType() == ParameterVariant::VariantType::AnimRecordPtrMap);
      ImGui::BeginDisabled(!var->m_editable && !isAnimRecords);

      // Animatable parameters carry a key diamond in front of their widget. The diamond decides on its
      // own whether there is anything to show (owner pushed and a clip bound).
      if (var->m_hint.animatable)
      {
        ShowKeyDiamond(var);
        ImGui::SameLine();
      }

      static bool lastValActive = false;
      if (callback)
      {
        var->m_onValueChangedFn.push_back(callback);
      }

      switch (var->GetType())
      {
        case ParameterVariant::VariantType::Bool:
        {
          bool val = var->GetVar<bool>();
          if (ImGui::Checkbox(var->m_name.c_str(), &val))
          {
            *var = val;
          }
        }
        break;
        case ParameterVariant::VariantType::Float:
        {
          static float lastVal = 0.0f;
          float val            = var->m_hint.waitForTheEndOfInput && lastValActive ? lastVal : var->GetVar<float>();

          if (!var->m_hint.isRangeLimited)
          {
            if (ImGui::InputFloat(var->m_name.c_str(), &val))
            {
              *var = val;
            }
          }
          else
          {
            bool dragged = false;
            if (ImGui::DragFloat(var->m_name.c_str(),
                                 &val,
                                 var->m_hint.increment,
                                 var->m_hint.rangeMin,
                                 var->m_hint.rangeMax))
            {
              if (!var->m_hint.waitForTheEndOfInput)
              {
                *var = val;
              }
              else
              {
                lastVal       = val;
                lastValActive = true;
              }
            }

            if (var->m_hint.waitForTheEndOfInput && ImGui::IsItemDeactivatedAfterEdit())
            {
              *var          = lastVal;
              lastValActive = false;
            }
          }
        }
        break;
        case ParameterVariant::VariantType::Int:
        {
          static int lastVal = 0;
          int val            = var->m_hint.waitForTheEndOfInput && lastValActive ? lastVal : var->GetVar<int>();

          if (var->m_hint.isRangeLimited)
          {
            if (ImGui::DragInt(var->m_name.c_str(),
                               &val,
                               var->m_hint.increment,
                               static_cast<int>(var->m_hint.rangeMin),
                               static_cast<int>(var->m_hint.rangeMax)))
            {
              if (!var->m_hint.waitForTheEndOfInput)
              {
                *var = val;
              }
              else
              {
                lastVal       = val;
                lastValActive = true;
              }
            }

            if (var->m_hint.waitForTheEndOfInput && ImGui::IsItemDeactivatedAfterEdit())
            {
              *var          = lastVal;
              lastValActive = false;
            }
          }
          else
          {
            if (ImGui::InputInt(var->m_name.c_str(), &val))
            {
              *var = val;
            }
          }
        }
        break;
        case ParameterVariant::VariantType::Vec2:
        {
          static Vec2 lastVal = Vec2(0.0f);
          Vec2 val            = var->m_hint.waitForTheEndOfInput && lastValActive ? lastVal : var->GetVar<Vec2>();

          if (var->m_hint.isRangeLimited)
          {
            if (ImGui::DragFloat2(var->m_name.c_str(),
                                  &val[0],
                                  var->m_hint.increment,
                                  var->m_hint.rangeMin,
                                  var->m_hint.rangeMax))
            {
              if (!var->m_hint.waitForTheEndOfInput)
              {
                *var = val;
              }
              else
              {
                lastVal       = val;
                lastValActive = true;
              }
            }

            if (var->m_hint.waitForTheEndOfInput && ImGui::IsItemDeactivatedAfterEdit())
            {
              *var          = lastVal;
              lastValActive = false;
            }
          }
          else
          {
            if (ImGui::DragFloat2(var->m_name.c_str(), &val[0], 0.1f))
            {
              *var = val;
            }
          }
        }
        break;
        case ParameterVariant::VariantType::Vec3:
        {
          Vec3 val = var->GetVar<Vec3>();
          if (var->m_hint.isColor)
          {
            if (ImGui::ColorEdit3(var->m_name.c_str(), &val[0], ImGuiColorEditFlags_NoLabel))
            {
              *var = val;
            }
          }
          else if (var->m_hint.isRangeLimited)
          {
            static Vec3 lastVal = Vec3(0.0f);
            val                 = var->m_hint.waitForTheEndOfInput && lastValActive ? lastVal : var->GetVar<Vec3>();

            if (ImGui::DragFloat3(var->m_name.c_str(),
                                  &val[0],
                                  var->m_hint.increment,
                                  var->m_hint.rangeMin,
                                  var->m_hint.rangeMax))
            {
              if (!var->m_hint.waitForTheEndOfInput)
              {
                *var = val;
              }
              else
              {
                lastVal       = val;
                lastValActive = true;
              }
            }

            if (var->m_hint.waitForTheEndOfInput && ImGui::IsItemDeactivatedAfterEdit())
            {
              *var          = lastVal;
              lastValActive = false;
            }
          }
          else
          {
            if (ImGui::DragFloat3(var->m_name.c_str(), &val[0], 0.1f))
            {
              *var = val;
            }
          }
        }
        break;
        case ParameterVariant::VariantType::Vec4:
        {
          Vec4 val = var->GetVar<Vec4>();
          if (var->m_hint.isColor)
          {
            if (ImGui::ColorEdit4(var->m_name.c_str(), &val[0], ImGuiColorEditFlags_NoLabel))
            {
              *var = val;
            }
          }
          else if (var->m_hint.isRangeLimited)
          {
            static Vec4 lastVal = Vec4(0.0f);
            val                 = var->m_hint.waitForTheEndOfInput && lastValActive ? lastVal : var->GetVar<Vec4>();

            if (ImGui::DragFloat4(var->m_name.c_str(),
                                  &val[0],
                                  var->m_hint.increment,
                                  var->m_hint.rangeMin,
                                  var->m_hint.rangeMax))
            {
              if (!var->m_hint.waitForTheEndOfInput)
              {
                *var = val;
              }
              else
              {
                lastVal       = val;
                lastValActive = true;
              }
            }

            if (var->m_hint.waitForTheEndOfInput && ImGui::IsItemDeactivatedAfterEdit())
            {
              *var          = lastVal;
              lastValActive = false;
            }
          }
          else
          {
            if (ImGui::DragFloat4(var->m_name.c_str(), &val[0], 0.1f))
            {
              *var = val;
            }
          }
        }
        break;
        case ParameterVariant::VariantType::String:
        {
          String val = var->GetVar<String>();
          if (ImGui::InputText(var->m_name.c_str(), &val) && IsTextInputFinalized())
          {
            *var = val;
          }
        }
        break;
        case ParameterVariant::VariantType::ObjectId:
        {
          ObjectId val = var->GetVar<ObjectId>();
          if (ImGui::InputScalar(var->m_name.c_str(), ImGuiDataType_U64, var->GetVarPtr<ObjectId>()) &&
              IsTextInputFinalized())
          {
            *var = val;
          }
        }
        break;
        case ParameterVariant::VariantType::MaterialPtr:
        {
          MaterialPtr& mref = var->GetVar<MaterialPtr>();
          String file, id;
          if (mref)
          {
            id   = std::to_string(mref->GetIdVal());
            file = mref->GetFile();
          }

          String uniqueName = var->m_name + "##" + id;
          ImGui::EndDisabled();
          ShowMaterialVariant(uniqueName, file, var);
          ImGui::BeginDisabled(!var->m_editable);
        }
        break;
        case ParameterVariant::VariantType::MeshPtr:
        {
          MeshPtr mref = var->GetVar<MeshPtr>();
          ImGui::EndDisabled();
          DropSubZone(
              "Mesh##" + std::to_string(mref->GetIdVal()),
              EditorImGuiTextureCache::Acquire(UI::m_meshIcon),
              mref->GetFile(),
              [&var](const DirectoryEntry& entry) -> void
              {
                if (GetResourceType(entry.m_ext) == Mesh::StaticClass())
                {
                  *var = GetMeshManager()->Create<Mesh>(entry.GetFullPath());
                }
                else if (GetResourceType(entry.m_ext) == SkinMesh::StaticClass())
                {
                  *var = GetMeshManager()->Create<SkinMesh>(entry.GetFullPath());
                }
                else
                {
                  TK_ERR("Only Mesh is accepted.");
                }
              },
              var->m_editable);
          ImGui::BeginDisabled(!var->m_editable);
        }
        break;
        case ParameterVariant::VariantType::HdriPtr:
        {
          HdriPtr mref = var->GetVar<HdriPtr>();
          String file, id;
          if (mref)
          {
            id   = std::to_string(mref->GetIdVal());
            file = mref->GetFile();
          }

          ImGui::EndDisabled();
          DropSubZone(
              "Hdri##" + id,
              EditorImGuiTextureCache::Acquire(UI::m_imageIcon),
              file,
              [&var](const DirectoryEntry& entry) -> void
              {
                if (GetResourceType(entry.m_ext) == Hdri::StaticClass())
                {
                  *var = GetTextureManager()->Create<Hdri>(entry.GetFullPath());
                }
                else
                {
                  TK_ERR("Only HDRI is accepted.");
                }
              },
              var->m_editable);
          ImGui::BeginDisabled(!var->m_editable);
        }
        break;
        case ParameterVariant::VariantType::SkeletonPtr:
        {
          SkeletonPtr mref = var->GetVar<SkeletonPtr>();
          String file, id;
          if (mref)
          {
            id   = std::to_string(mref->GetIdVal());
            file = mref->GetFile();
          }

          auto dropZoneFnc = [&var, &comp](const DirectoryEntry& entry) -> void
          {
            if (GetResourceType(entry.m_ext) == Skeleton::StaticClass())
            {
              *var = GetSkeletonManager()->Create<Skeleton>(entry.GetFullPath());
              if (SkeletonComponent* scom = comp->As<SkeletonComponent>())
              {
                scom->Init();
              }
            }
            else
            {
              TK_ERR("Only Skeleton is accepted.");
            }
          };
          ImGui::EndDisabled();
          DropSubZone("Skeleton##" + id,
                      EditorImGuiTextureCache::Acquire(UI::m_boneIcon),
                      file,
                      dropZoneFnc,
                      var->m_editable);
          ImGui::BeginDisabled(!var->m_editable);
        }
        break;
        case ParameterVariant::VariantType::AnimRecordPtrMap:
        {
          ComponentView::ShowAnimControllerComponent(var, comp);
        }
        break;
        case ParameterVariant::VariantType::VariantCallback:
        {
          if (UI::BeginCenteredTextButton(var->m_name))
          {
            VariantCallback callback = var->GetVar<VariantCallback>();
            callback();
          }
          UI::EndCenteredTextButton();
        }
        break;
        case ParameterVariant::VariantType::MultiChoice:
        {
          MultiChoiceVariant* mcv = var->GetVarPtr<MultiChoiceVariant>();
          String name             = var->m_name + "##MultiChoiceVariant";
          if (ImGui::BeginCombo(name.c_str(), mcv->Choices[mcv->CurrentVal.Index].m_name.c_str()))
          {
            for (uint i = 0; i < mcv->Choices.size(); ++i)
            {
              bool isSelected = i == mcv->CurrentVal.Index;
              if (ImGui::Selectable(mcv->Choices[i].m_name.c_str(), isSelected))
              {
                Value oldVal    = mcv->CurrentVal.Index;
                Value newVal    = i;
                mcv->CurrentVal = i;

                if (callback)
                {
                  callback(oldVal, newVal);
                }
              }
            }
            ImGui::EndCombo();
          }
        }
        break;
        default:
          break;
      }

      if (callback)
      {
        var->m_onValueChangedFn.pop_back();
      }

      ImGui::EndDisabled();
    }

    CustomDataView::CustomDataView() : View("Custom Data View")
    {
      m_viewID  = 4;
      m_viewIcn = UI::m_objectDataIcon;
    }

    CustomDataView::~CustomDataView() {}

    void CustomDataView::Show()
    {
      m_entity      = GetApp()->GetCurrentScene()->GetCurrentSelection();
      EntityPtr ntt = m_entity.lock();

      if (ntt == nullptr)
      {
        ImGui::Text("Select an entity");
        return;
      }

      IntArray customParams;
      ntt->m_localData.GetByCategory(CustomDataCategory.Name, customParams);
      ShowCustomData(ntt, "Custom Data", customParams, true);
    }

  } // namespace Editor
} // namespace ToolKit