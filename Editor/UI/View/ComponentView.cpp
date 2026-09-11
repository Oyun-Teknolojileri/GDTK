/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "ComponentView.h"

#include "Action.h"
#include "App.h"
#include "CustomDataView.h"
#include "EditorScene.h"

#include <AABBOverrideComponent.h>
#include <AnimationControllerComponent.h>
#include <EnvironmentComponent.h>
#include <FileManager.h>
#include <Material.h>
#include <Mesh.h>

#include <algorithm>

namespace ToolKit
{
  namespace Editor
  {

    // Editor only root motion preview support. Root motion is applied to the
    // entity node by the engine every frame; the preview remembers the node
    // transform when it starts so Stop (or switching to another clip) can put
    // the entity back, keeping the edited pose intact in the scene.
    //
    // The state is file scope rather than a function local static so that
    // ComponentView::ReleaseViewState() can drop it while the engine is still alive.
    // It holds an AnimRecordPtr, which in turn owns an AnimationPtr, so letting the
    // process exit clean up after it would release an engine resource after ToolKit is gone.
    namespace
    {
      struct AnimPreviewState
      {
        bool hasBase = false;
        Mat4 baseLocal = Mat4(1.0f);
      };

      struct AnimControllerViewState
      {
        // The "add a new track" row. Holds the animation the user dropped and the signal
        // name until the row is committed, so it has to survive between frames.
        std::pair<String, AnimRecordPtr> extraTrack;

        std::unordered_map<ObjectId, AnimPreviewState> previewStates;

        /** Lazily allocates the scratch record, which needs a live object registry. */
        void EnsureExtraTrack()
        {
          if (extraTrack.second == nullptr)
          {
            extraTrack.second = MakeNewPtr<AnimRecord>();
          }
        }

        void Release()
        {
          extraTrack.first  = "";
          extraTrack.second = nullptr;
          previewStates.clear();
        }
      };

      AnimControllerViewState& GetViewState()
      {
        static AnimControllerViewState state;
        return state;
      }

      void ReleaseViewStateImp()
      {
        GetViewState().Release();
      }

      AnimPreviewState& GetPreviewState(AnimControllerComponent* comp)
      {
        return GetViewState().previewStates[comp->GetIdVal()];
      }

      void StorePreviewBase(AnimControllerComponent* comp)
      {
        if (EntityPtr ntt = comp->OwnerEntity())
        {
          AnimPreviewState& state = GetPreviewState(comp);
          state.baseLocal         = ntt->m_node->GetTransform(TransformationSpace::TS_LOCAL);
          state.hasBase           = true;
        }
      }

      void RestorePreviewBase(AnimControllerComponent* comp, bool clear)
      {
        auto& previewStates = GetViewState().previewStates;
        auto it             = previewStates.find(comp->GetIdVal());
        if (it == previewStates.end())
        {
          return;
        }

        AnimPreviewState& state = it->second;
        if (state.hasBase)
        {
          if (EntityPtr ntt = comp->OwnerEntity())
          {
            ntt->m_node->SetTransform(state.baseLocal, TransformationSpace::TS_LOCAL);
          }
        }

        if (clear)
        {
          previewStates.erase(it);
        }
      }
    }

    void ComponentView::ReleaseViewState() { ReleaseViewStateImp(); }

    void ShowMultiMaterialComponent(ComponentPtr& comp,
                                    std::function<bool(const String&)> showCompFunc,
                                    bool modifiableComp)
    {
      MaterialComponent* mmComp = (MaterialComponent*) comp.get();
      MaterialPtrArray& matList = mmComp->GetMaterialList();
      bool isOpen               = showCompFunc(MaterialComponentCategory.Name);

      if (isOpen)
      {
        ImGui::BeginDisabled(!modifiableComp);

        uint removeMaterialIndx = TK_UINT_MAX;
        for (uint i = 0; i < matList.size(); i++)
        {
          MaterialPtr& mat = matList[i];
          String path, fileName, ext;
          DecomposePath(mat->GetFile(), &path, &fileName, &ext);
          if (fileName.empty())
          {
            fileName = mat->m_name;
          }

          String uniqueName = fileName + "##" + std::to_string(i);
          ImGui::PushID(i);
          // push red color for X
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
          if (UI::ButtonDecorless(ICON_FA_TIMES, Vec2(15)))
          {
            removeMaterialIndx = i;
          }
          ImGui::PopStyleColor();

          ImGui::SameLine();
          ImGui::EndDisabled();
          CustomDataView::ShowMaterialPtr(uniqueName, mat->GetFile(), mat, modifiableComp);
          ImGui::BeginDisabled(!modifiableComp);
          ImGui::PopID();
        }
        if (removeMaterialIndx != TK_UINT_MAX)
        {
          mmComp->RemoveMaterial(removeMaterialIndx);
        }

        if (UI::BeginCenteredTextButton("Update"))
        {
          mmComp->UpdateMaterialList();
        }
        UI::EndCenteredTextButton();
        ImGui::SameLine();
        if (ImGui::Button("Add"))
        {
          mmComp->AddMaterial(GetMaterialManager()->GetCopyOfDefaultMaterial());
        }
        UI::HelpMarker("Update", "Update material list by first MeshComponent's mesh list");

        ImGui::EndDisabled();
      }
    }

    void ComponentView::ShowAnimControllerComponent(ParameterVariant* var, ComponentPtr comp)
    {
      AnimRecordPtrMap& mref = var->GetVar<AnimRecordPtrMap>();
      String file;

      AnimControllerComponent* animPlayerComp = comp->As<AnimControllerComponent>();

      // If component isn't AnimationPlayerComponent, don't show variant.
      if (animPlayerComp == nullptr)
      {
        TK_ERR("AnimRecordPtrMap is for AnimationControllerComponent.");
        return;
      }

      if (animPlayerComp->GetActiveRecord())
      {
        String file;
        DecomposePath(animPlayerComp->GetActiveRecord()->m_animation->GetFile(), nullptr, &file, nullptr);

        String text = Format("Animation: %s, Duration: %f, T: %f",
                             file.c_str(),
                             animPlayerComp->GetActiveRecord()->m_animation->m_duration,
                             animPlayerComp->GetActiveRecord()->m_currentTime);

        ImGui::Text(text.c_str());
      }

      // Small breathing room under the component header.
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // Resize the record table vertically by dragging its bottom border.
      if (ImGui::BeginChild("AnimationRecordsResizable", ImVec2(-FLT_MIN, 200.0f), ImGuiChildFlags_ResizeY))
      {
        const ImVec2 tableSize = ImGui::GetContentRegionAvail();
        if (ImGui::BeginTable("Animation Records and Signals",
                              6,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_Reorderable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                                  ImGuiTableFlags_SortTristate | ImGuiTableFlags_NoSavedSettings,
                              tableSize))
        {
          float tableWdth = ImGui::GetItemRectSize().x;
          // Name shows the animation file and is read only. Signal is the
          // unique record key used to trigger the track (Play(signal)) and may
          // be edited by the user. Rows keep their insertion order; sorting is
          // applied only when the Name or Signal header is clicked.
          ImGui::TableSetupColumn("Animation",
                                  ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort,
                                  tableWdth / 6.0f);
          ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, tableWdth / 3.0f);
          ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch, tableWdth / 3.0f);
          ImGui::TableSetupColumn("Preview",
                                  ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort,
                                  tableWdth / 6.0f);
          ImGui::TableSetupColumn("Apply Root Motion",
                                  ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort,
                                  tableWdth / 6.0f);
          ImGui::TableSetupColumn("Remove",
                                  ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort,
                                  tableWdth / 12.0f);
          // Freeze the header row like an Excel frozen top row; body rows scroll
          // under it when the table overflows.
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableHeadersRow();

          uint rowIndx          = 0;
          String removedSignal  = "";
          String renamedSignal  = "";
          std::pair<String, AnimRecordPtr> renameSource; // Old key of the edited track.

          // View state kept across frames, released by ComponentView::ReleaseViewState().
          AnimControllerViewState& viewState = GetViewState();
          viewState.EnsureExtraTrack();
          std::pair<String, AnimRecordPtr>& extraTrack = viewState.extraTrack;

          // Animation file name of a record. Read only "Name" column content.
          auto trackDisplayName = [](const AnimRecordPtr& record) -> String
          {
            if (record == nullptr || record->m_animation == nullptr)
            {
              return "";
            }

            String path, fileName, ext;
            DecomposePath(record->m_animation->GetFile(), &path, &fileName, &ext);
            return fileName + ext;
          };

          // Signal names (record keys) must stay unique across the tracks.
          auto signalExists = [&mref](const String& signal) -> bool { return mref.Contains(signal); };

          // Animation DropZone. Returns the drawn dropzone height so callers can
          // vertically center the rest of the row's cells against it.
          auto showAnimationDropzone =
              [file, &mref](uint& columnIndx, std::pair<String, AnimRecordPtr>& pair) -> float
          {
            ImGui::TableSetColumnIndex(columnIndx++);

            // Center the dropzone horizontally in its cell. It is the tallest
            // item of the row, so it defines the row height and needs no
            // vertical centering.
            const float dzSize = 48.0f;
            float availX       = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + glm::max(0.0f, (availX - dzSize) * 0.5f));

            DropZone(EditorImGuiTextureCache::Acquire(UI::m_clipIcon),
                     file,
                     [&pair, &mref](const DirectoryEntry& entry) -> void
                     {
                       if (GetResourceType(entry.m_ext) != Animation::StaticClass())
                       {
                         GetApp()->SetStatusMsg(g_statusFailed);
                         TK_ERR("Only animations are accepted.");
                         return;
                       }

                       // Normalize both sides to a resource-relative key so the
                       // same file can not be assigned to two different tracks
                       // even when one path is absolute and the other relative.
                       String dropped = entry.GetFullPath();
                       UnixifyPath(dropped);
                       GetFileManager()->GetRelativeResourcesPath(dropped);
                       size_t first = dropped.find_first_not_of('/');
                       if (first != String::npos)
                       {
                         dropped = dropped.substr(first);
                       }

                       for (const auto& rec : mref)
                       {
                         if (rec.second == pair.second)
                         {
                           continue; // Re-dropping the same file onto its own track.
                         }

                         const AnimationPtr anim = rec.second->m_animation;
                         if (anim == nullptr)
                         {
                           continue;
                         }

                         String existing = anim->GetFile();
                         UnixifyPath(existing);
                         GetFileManager()->GetRelativeResourcesPath(existing);
                         first = existing.find_first_not_of('/');
                         if (first != String::npos)
                         {
                           existing = existing.substr(first);
                         }

                         if (existing == dropped)
                         {
                           GetApp()->SetStatusMsg(g_statusFailed);
                           TK_ERR("Animation %s is already used by track '%s'.",
                                  entry.m_fileName.c_str(),
                                  rec.first.c_str());
                           return;
                         }
                       }

                       pair.second->m_animation = GetAnimationManager()->Create<Animation>(entry.GetFullPath());
                       if (pair.first.empty())
                       {
                         // New track: default the signal to the animation name.
                         pair.first = entry.m_fileName;
                       }
                     });

            return ImGui::GetItemRectSize().y;
          };

          // Read only animation file name cell.
          auto showNameCell = [&trackDisplayName](uint& columnIndx,
                                                  const std::pair<String, AnimRecordPtr>& pair,
                                                  float cellContentH)
          {
            ImGui::TableSetColumnIndex(columnIndx++);

            String name  = trackDisplayName(pair.second);
            float txtH   = ImGui::GetTextLineHeight();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + glm::max(0.0f, (cellContentH - txtH) * 0.5f));
            ImGui::TextUnformatted(name.c_str());
          };

          // Editable signal cell. The new key is remembered on enter and applied
          // after the rows are drawn, so the table order stays stable.
          auto showSignalCell = [&renamedSignal, &renameSource](uint& columnIndx,
                                                                const std::pair<String, AnimRecordPtr>& pair,
                                                                float cellContentH)
          {
            ImGui::TableSetColumnIndex(columnIndx++);

            float frameH = ImGui::GetFrameHeight();
            float availX = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + glm::max(0.0f, (cellContentH - frameH) * 0.5f));
            ImGui::SetNextItemWidth(availX);

            String signal = pair.first;
            if (ImGui::InputText("##Signal", &signal, ImGuiInputTextFlags_EnterReturnsTrue) && signal.length())
            {
              renamedSignal = signal;
              renameSource  = std::make_pair(pair.first, pair.second);
            }
          };

          // Render rows in insertion order. Sorting only kicks in when the Name
          // or Signal header is clicked; it sorts a transient index copy.
          std::vector<uint> rowOrder;
          rowOrder.reserve(mref.size());
          for (uint i = 0; i < (uint) mref.size(); i++)
          {
            rowOrder.push_back(i);
          }

          bool sortRows   = false;
          bool sortByName = false;
          bool sortAsc    = true;
          if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
          {
            if (sortSpecs->SpecsCount > 0 &&
                (sortSpecs->Specs[0].ColumnIndex == 1 || sortSpecs->Specs[0].ColumnIndex == 2))
            {
              sortRows   = true;
              sortByName = (sortSpecs->Specs[0].ColumnIndex == 1);
              sortAsc    = (sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
            }
          }

          if (sortRows)
          {
            std::sort(rowOrder.begin(),
                      rowOrder.end(),
                      [&mref, &trackDisplayName, sortByName, sortAsc](uint a, uint b) -> bool
                      {
                        const String valA = sortByName ? trackDisplayName(mref[a].second) : mref[a].first;
                        const String valB = sortByName ? trackDisplayName(mref[b].second) : mref[b].first;
                        if (valA == valB)
                        {
                          return a < b; // Stable tie breaker.
                        }
                        return sortAsc ? (valA < valB) : (valA > valB);
                      });
          }

          for (uint displayIndx = 0; displayIndx < (uint) rowOrder.size(); displayIndx++, rowIndx++)
          {
            std::pair<String, AnimRecordPtr>& track = mref[rowOrder[displayIndx]];
            uint columnIndx                         = 0;
            ImGui::TableNextRow();
            ImGui::PushID(rowIndx);

            // Animation file cell. Disabled when the parameter is read only.
            ImGui::BeginDisabled(!var->m_editable);
            const float cellContentH = showAnimationDropzone(columnIndx, track);
            ImGui::EndDisabled();

            // Read only animation file name.
            showNameCell(columnIndx, track, cellContentH);

            // Editable, unique signal (record key).
            ImGui::BeginDisabled(!var->m_editable);
            showSignalCell(columnIndx, track, cellContentH);
            ImGui::EndDisabled();

            // Play, Pause & Stop Buttons. Always usable.
            {
              const float btnH = 24.0f;
              ImGui::TableSetColumnIndex(columnIndx++);

              if (track.second->m_animation)
              {
                // Image buttons grow by the frame padding; measure the actual
                // drawn sizes so the play/pause-stop group is centered on its
                // real footprint.
                const ImVec2 framePad = ImGui::GetStyle().FramePadding;
                const float iconW     = btnH + 2.0f * framePad.x;
                const float iconH     = btnH + 2.0f * framePad.y;

                float availX = ImGui::GetContentRegionAvail().x;
                float totalW = iconW + ImGui::GetStyle().ItemSpacing.x + iconW;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + glm::max(0.0f, (availX - totalW) * 0.5f));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + glm::max(0.0f, (cellContentH - iconH) * 0.5f));

                AnimRecordPtr activeRecord = animPlayerComp->GetActiveRecord();

                // Alternate between Play - Pause buttons.
                if (activeRecord == track.second && activeRecord->m_state == AnimRecord::State::Play)
                {
                  if (UI::ImageButtonDecorless(EditorImGuiTextureCache::Acquire(UI::m_pauseIcon), Vec2(24, 24)))
                  {
                    animPlayerComp->Pause();
                  }
                }
                else if (UI::ImageButtonDecorless(EditorImGuiTextureCache::Acquire(UI::m_playIcon), Vec2(24, 24)))
                {
                  if (activeRecord == track.second && activeRecord->m_state == AnimRecord::State::Pause)
                  {
                    // Editor preview only: resume the paused clip from the same
                    // time and place; root motion keeps accumulating from the
                    // paused position.
                    animPlayerComp->Resume();
                  }
                  else if (activeRecord != nullptr)
                  {
                    // Editor preview only: another clip is playing. Stop it and
                    // revert its root motion first so displacements never stack
                    // and the edited pose is preserved.
                    animPlayerComp->Stop();
                    RestorePreviewBase(animPlayerComp, false);
                    if (!GetPreviewState(animPlayerComp).hasBase)
                    {
                      StorePreviewBase(animPlayerComp);
                    }

                    animPlayerComp->Play(track.first.c_str());
                  }
                  else
                  {
                    // Editor preview only: remember the entity transform before
                    // root motion starts.
                    StorePreviewBase(animPlayerComp);
                    animPlayerComp->Play(track.first.c_str());
                  }
                }

                // Draw stop button always.
                ImGui::SameLine();
                if (UI::ImageButtonDecorless(EditorImGuiTextureCache::Acquire(UI::m_stopIcon), Vec2(24, 24)))
                {
                  // Editor preview only: revert the accumulated root motion and
                  // forget the stored base pose.
                  RestorePreviewBase(animPlayerComp, true);
                  animPlayerComp->Stop();
                }
              }
            }

            // Apply Root Motion
            {
              const float checkH = ImGui::GetFrameHeight();
              ImGui::TableSetColumnIndex(columnIndx++);

              float availX = ImGui::GetContentRegionAvail().x;
              ImGui::SetCursorPosX(ImGui::GetCursorPosX() + glm::max(0.0f, (availX - checkH) * 0.5f));
              ImGui::SetCursorPosY(ImGui::GetCursorPosY() + glm::max(0.0f, (cellContentH - checkH) * 0.5f));

              ImGui::BeginDisabled(!var->m_editable);
              bool applyRootMotion = track.second->m_applyRootMotion;
              if (ImGui::Checkbox("##applyRootMotion", &applyRootMotion))
              {
                track.second->m_applyRootMotion = applyRootMotion;
              }
              ImGui::EndDisabled();
            }

            // Remove Button
            {
              const float rmSize = 18.0f;
              ImGui::TableSetColumnIndex(columnIndx++);

              float availX = ImGui::GetContentRegionAvail().x;
              ImGui::SetCursorPosX(ImGui::GetCursorPosX() + glm::max(0.0f, (availX - rmSize) * 0.5f));
              ImGui::SetCursorPosY(ImGui::GetCursorPosY() + glm::max(0.0f, (cellContentH - rmSize) * 0.5f));

              // Same X glyph used by the component header row, white to match it.
              ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
              ImGui::BeginDisabled(!var->m_editable);
              if (UI::ButtonDecorless(ICON_FA_TIMES, ImVec2(rmSize, rmSize)))
              {
                removedSignal = track.first;
              }
              ImGui::EndDisabled();
              ImGui::PopStyleColor();
            }

            ImGui::PopID();
          }

          // Show last extra track.
          {
            uint columnIndx = 0;
            ImGui::TableNextRow();
            ImGui::PushID(rowIndx);

            ImGui::BeginDisabled(!var->m_editable);
            const float cellContentH = showAnimationDropzone(columnIndx, extraTrack);
            ImGui::EndDisabled();

            // Read only animation file name.
            showNameCell(columnIndx, extraTrack, cellContentH);

            // Editable, unique signal (record key).
            ImGui::BeginDisabled(!var->m_editable);
            showSignalCell(columnIndx, extraTrack, cellContentH);
            ImGui::EndDisabled();

            ImGui::PopID();
          }

          if (removedSignal.length())
          {
            animPlayerComp->RemoveSignal(removedSignal);
          }

          if (renamedSignal.length() && renameSource.first != renamedSignal)
          {
            if (signalExists(renamedSignal))
            {
              TK_ERR("SignalName exists.");
            }
            else if (renameSource.first == extraTrack.first)
            {
              extraTrack.first = renamedSignal;
            }
            else
            {
              mref.Rename(renameSource.first, renamedSignal);
            }
          }

          // If the extra track is filled properly, add it to the list.
          if (extraTrack.first != "" && extraTrack.second->m_animation != nullptr)
          {
            if (signalExists(extraTrack.first))
            {
              TK_ERR("SignalName exists.");
              extraTrack.first = ""; // Let the user pick another signal.
            }
            else
            {
              mref.Insert(extraTrack.first, extraTrack.second);
              extraTrack.first  = "";
              extraTrack.second = MakeNewPtr<AnimRecord>();
            }
          }

          ImGui::EndTable();
        }

        ImGui::EndChild();
      }
    }

    bool ComponentView::ShowComponentBlock(ComponentPtr& comp, const bool modifiableComp)
    {
      VariantCategoryArray categories;
      comp->m_localData.GetCategories(categories, true, true);

      bool removeComp   = false;
      auto showCompFunc = [comp, &removeComp, modifiableComp](const String& headerName) -> bool
      {
        ImGui::PushID((int) comp->GetIdVal());
        String varName = headerName + "##" + std::to_string(modifiableComp);
        bool isOpen    = ImGui::CollapsingHeader(varName.c_str(), nullptr, ImGuiTreeNodeFlags_AllowOverlap);

        if (modifiableComp)
        {
          float offset = ImGui::GetContentRegionAvail().x - 30.0f;
          ImGui::SameLine(offset);
          if (UI::ButtonDecorless(ICON_FA_TIMES, // X
                                  ImVec2(15.0f, 15.0f)) &&
              !removeComp)
          {
            GetApp()->SetStatusMsg(headerName + " " + g_statusRemoved);
            removeComp = true;
          }
        }
        ImGui::PopID();

        return isOpen;
      };

      ImGui::Indent();

      if (comp->IsA<MaterialComponent>())
      {
        ShowMultiMaterialComponent(comp, showCompFunc, modifiableComp);
      }
      else
      {
        // Show header if no categories exist.
        if (categories.empty())
        {
          showCompFunc(comp->Class()->Name);
        }
        else
        {
          // Show each parameter under corresponding category.
          for (VariantCategory& category : categories)
          {
            bool isOpen = showCompFunc(category.Name);

            if (isOpen)
            {
              ParameterVariantRawPtrArray vars;
              comp->m_localData.GetByCategory(category.Name, vars);

              for (ParameterVariant* var : vars)
              {
                bool editable = var->m_editable;
                if (!modifiableComp)
                {
                  var->m_editable = false;
                }
                ValueUpdateFn multiUpdate = CustomDataView::MultiUpdate(var, comp->Class());
                var->m_onValueChangedFn.push_back(multiUpdate);
                CustomDataView::ShowVariant(var, comp);
                var->m_onValueChangedFn.pop_back();
                if (!modifiableComp)
                {
                  var->m_editable = true;
                }
              }
            }
          }
        }
      }

      if (removeComp)
      {
        if (comp->IsA<SkeletonComponent>())
        {
          MeshComponentPtr mesh = comp->OwnerEntity()->GetComponent<MeshComponent>();

          if (mesh != nullptr && mesh->GetMeshVal()->IsSkinned())
          {
            GetApp()->SetStatusMsg(g_statusFailed);
            TK_WRN("Skeleton component is in use, it can't be removed.");
            return false;
          }
        }
      }

      ImGui::Unindent();
      return removeComp;
    }

    // ComponentView
    //////////////////////////////////////////

    ComponentView::ComponentView() : View("Component View")
    {
      m_viewID  = 3;
      m_viewIcn = UI::m_packageIcon;
    }

    ComponentView::~ComponentView() {}

    void ComponentView::Show()
    {
      m_entity      = GetApp()->GetCurrentScene()->GetCurrentSelection();
      EntityPtr ntt = m_entity.lock();

      if (ntt == nullptr)
      {
        ImGui::Text("Select an entity");
        return;
      }

      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);

      UI::PushBoldFont();
      if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen))
      {
        UI::PopBoldFont();

        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, g_indentSpacing);
        ImGui::Indent();

        std::vector<ClassMeta*> compRemove;
        for (auto& com : ntt->GetComponentPtrArray())
        {
          ImGui::Spacing();
          if (ShowComponentBlock(com, true))
          {
            compRemove.push_back(com->Class());
          }
        }

        for (ClassMeta* Class : compRemove)
        {
          ActionManager::GetInstance()->AddAction(new DeleteComponentAction(ntt->GetComponent(Class)));
        }

        // Remove billboards if necessary.
        ScenePtr scene          = GetSceneManager()->GetCurrentScene();
        EditorScenePtr edtScene = Cast<EditorScene>(scene);
        edtScene->ValidateBillboard(ntt);

        ImGui::Separator();

        // Draw the centered button
        if (UI::BeginCenteredTextButton("Add Component"))
        {
          // Open the popup manually
          ImGui::OpenPopup("##NewComponentMenu");
        }
        UI::EndCenteredTextButton();

        // Get button rect (for positioning popup below)
        ImVec2 buttonMin = ImGui::GetItemRectMin();
        ImVec2 buttonMax = ImGui::GetItemRectMax();
        ImVec2 popupPos(buttonMin.x, buttonMax.y);

        // Set the popup position to appear right below the button
        ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
        ImGui::PushItemWidth(150);

        if (ImGui::BeginPopup("##NewComponentMenu"))
        {
          App* editor         = GetApp();
          bool componentAdded = false;

          if (ImGui::MenuItem("Mesh Component"))
          {
            if (ntt->GetComponentFast<MeshComponent>() == nullptr)
            {
              ntt->AddComponent<MeshComponent>();
              componentAdded = true;

              editor->SetStatusMsg(g_successStr);
            }
            else
            {
              editor->SetStatusMsg(g_statusFailed);
              TK_WRN("Mesh Component already exists.");
            }
          }

          if (ImGui::MenuItem("Material Component"))
          {
            if (ntt->GetComponentFast<MaterialComponent>() == nullptr)
            {
              MaterialComponentPtr mmComp = ntt->AddComponent<MaterialComponent>();
              mmComp->UpdateMaterialList();
              componentAdded = true;

              editor->SetStatusMsg(g_successStr);
            }
            else
            {
              editor->SetStatusMsg(g_statusFailed);
              TK_WRN("Material Component already exists.");
            }
          }

          if (ImGui::MenuItem("Environment Component"))
          {
            if (ntt->GetComponentFast<EnvironmentComponent>() == nullptr)
            {
              // A default hdri must be given for component creation via editor.
              // Create a default hdri.
              TextureManager* texMan         = GetTextureManager();
              HdriPtr hdri                   = texMan->Create<Hdri>(texMan->GetDefaultResource(Hdri::StaticClass()));

              EnvironmentComponentPtr envCom = MakeNewPtr<EnvironmentComponent>();
              envCom->SetHdriVal(hdri);

              ntt->AddComponent(envCom);
              componentAdded = true;

              editor->SetStatusMsg(g_successStr);
            }
            else
            {
              editor->SetStatusMsg(g_statusFailed);
              TK_WRN("Environment Component already exists.");
            }
          }

          if (ImGui::MenuItem("Animation Controller Component"))
          {
            if (ntt->GetComponentFast<AnimControllerComponent>() == nullptr)
            {
              ntt->AddComponent<AnimControllerComponent>();
              componentAdded = true;
              editor->SetStatusMsg(g_successStr);
            }
            else
            {
              editor->SetStatusMsg(g_statusFailed);
              TK_WRN("Animation Controller Component already exists.");
            }
          }

          if (ImGui::MenuItem("Skeleton Component"))
          {
            if (ntt->GetComponentFast<SkeletonComponent>() == nullptr)
            {
              // Check if mesh component is skinned.
              MeshComponentPtr meshComp = ntt->GetComponent<MeshComponent>();
              if (meshComp && meshComp->GetMeshVal()->IsSkinned())
              {
                ntt->AddComponent<SkeletonComponent>();
                componentAdded = true;
                editor->SetStatusMsg(g_successStr);
              }
              else
              {
                editor->SetStatusMsg(g_statusFailed);
                TK_WRN("Skeleton Component can only be added to skinned meshes.");
              }
            }
            else
            {
              editor->SetStatusMsg(g_statusFailed);
              TK_WRN("Skeleton Component already exists.");
            }
          }

          if (ImGui::MenuItem("AABB Override Component"))
          {
            if (ntt->GetComponentFast<AABBOverrideComponent>() == nullptr)
            {
              ntt->AddComponent<AABBOverrideComponent>();
              componentAdded = true;
              editor->SetStatusMsg(g_successStr);
            }
            else
            {
              editor->SetStatusMsg(g_statusFailed);
              TK_WRN("AABB Override Component already exists.");
            }
          }

          // Create dynamic menu.
          ImGui::Separator();
          for (DynamicMenuPtr root : editor->m_customComponentsMenu)
          {
            ShowDynamicMenu(root,
                            [ntt, &componentAdded](const StringView& className) -> void
                            {
                              App* editor = GetApp();
                              if (ComponentPtr cmp = MakeNewPtrCasted<Component>(className))
                              {
                                if (ntt->GetComponent(cmp->Class()) == nullptr)
                                {
                                  ntt->AddComponent(cmp);
                                  componentAdded = true;

                                  editor->SetStatusMsg(g_successStr);
                                }
                                else
                                {
                                  editor->SetStatusMsg(g_statusFailed);
                                  TK_WRN("Component already exists: %s", className.data());
                                }
                              }
                            });
          }

          // State changed this means a new component has been added.
          if (componentAdded)
          {
            edtScene->AddBillboard(ntt);
          }

          ImGui::EndPopup();
        }
        ImGui::Unindent();

        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
      }
      else
      {
        UI::PopBoldFont();
      }
    }

  } // namespace Editor
} // namespace ToolKit