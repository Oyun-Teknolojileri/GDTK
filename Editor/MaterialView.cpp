/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "MaterialView.h"

#include "App.h"
#include "EditorScene.h"
#include "EditorViewport.h"
#include "PreviewViewport.h"

#include <DirectionComponent.h>
#include <FileManager.h>
#include <GradientSky.h>
#include <Material.h>

namespace ToolKit
{
  namespace Editor
  {

    // MaterialView
    //////////////////////////////////////////

    MaterialView::MaterialView() : View("Material View")
    {
      m_viewID   = 3;
      m_viewIcn  = UI::m_materialIcon;

      m_viewport = MakeNewPtr<PreviewViewport>();
      m_viewport->Init({300.0f, 150.0f});

      SceneManager* scnMan = GetSceneManager();
      m_scenes[0]          = scnMan->Create<Scene>(ScenePath("ms-sphere.scene", true));
      m_scenes[1]          = scnMan->Create<Scene>(ScenePath("ms-box.scene", true));
      m_scenes[2]          = scnMan->Create<Scene>(ScenePath("ms-ball.scene", true));

      m_viewport->SetScene(m_scenes[0]);

      ResetCamera();
    }

    MaterialView::~MaterialView() { m_viewport = nullptr; }

    void MaterialView::SetSelectedMaterial(MaterialPtr mat)
    {
      auto find = std::find(m_materials.cbegin(), m_materials.cend(), mat);
      if (find != m_materials.cend())
      {
        m_currentMaterialIndex = int(find - m_materials.cbegin());
      }
    }

    void MaterialView::SetMaterials(const MaterialPtrArray& mat) { m_materials = mat; }

    void MaterialView::ResetCamera()
    {
      CameraPtr cam = m_viewport->GetCamera();
      cam->FocusToBoundingBox(BoundingBox(Vec3(-0.5f, -1.0f, -0.5f), Vec3(0.5f, 2.8f, -0.5f)), 1.1f);
    }

    void MaterialView::UpdatePreviewScene()
    {
      m_viewport->SetScene(m_scenes[m_activeObjectIndx]);

      // Perform material change as a render task, because consecutive preview renders may override the
      // preview scene's material, which causes wrong materials to appear in the preview.
      GetRenderSystem()->AddRenderTask({[this](Renderer* renderer) -> void
                                        {
                                          EntityPtrArray materialNtties = m_viewport->GetScene()->GetByTag("target");
                                          for (EntityPtr ntt : materialNtties)
                                          {
                                            ntt->GetMaterialComponent()->SetFirstMaterial(
                                                m_materials[m_currentMaterialIndex]);
                                          }
                                        }});
    }

    int DrawTypeToInt(DrawType drawType)
    {
      switch (drawType)
      {
      case DrawType::Triangle:
        return 0;
      case DrawType::Line:
        return 1;
      case DrawType::LineStrip:
        return 2;
      case DrawType::LineLoop:
        return 3;
      case DrawType::Point:
        return 4;
      default:
        return -1;
      }
    }

    DrawType IntToDrawType(int drawType)
    {
      switch (drawType)
      {
      case 0:
        return DrawType::Triangle;
      case 1:
        return DrawType::Line;
      case 2:
        return DrawType::LineStrip;
      case 3:
        return DrawType::LineLoop;
      case 4:
        return DrawType::Point;
      default:
        return DrawType::Triangle;
      }
    }

    void MaterialView::ShowMaterial(MaterialPtr mat)
    {
      if (!mat)
      {
        ImGui::Text("\nSelect a material");
        return;
      }

      String name, ext, path;
      DecomposePath(mat->GetFile(), &path, &name, &ext);
      UI::HeaderText(name.c_str());
      GetFileManager()->GetRelativeResourcesPath(path);
      UI::HelpMarker(TKLoc, path.c_str());

      if (ImGui::CollapsingHeader("Material Preview", ImGuiTreeNodeFlags_DefaultOpen))
      {
        const Vec2 iconSize = Vec2(16.0f, 16.0f);
        const Vec2 spacing  = ImGui::GetStyle().ItemSpacing;
        UpdatePreviewScene();
        if (UI::ImageButtonDecorless(UI::m_cameraIcon->m_textureId, iconSize, false))
        {
          ResetCamera();
        }

        const Vec2 viewportSize = Vec2(ImGui::GetContentRegionAvail().x - iconSize.x - 9.0f * spacing.x, 130.0f);
        if (viewportSize.x > 1.0f && viewportSize.y > 1.0f)
        {
          ImGui::SameLine();
          m_viewport->m_isTempView = m_isTempView;
          m_viewport->SetViewportSize((uint) viewportSize.x, (uint) viewportSize.y);
          m_viewport->Update(g_app->GetDeltaTime());
          m_viewport->Show();
          ImGui::SameLine();
          ImGui::BeginGroup();

          auto setIconFn = [&](TexturePtr icon, uint id) -> void
          {
            char iconId[16];
            snprintf(iconId, sizeof(iconId), "##icon%d", id);
            if (ImGui::ImageButton(iconId, Convert2ImGuiTexture(icon), iconSize))
            {
              m_activeObjectIndx = id;
            }
          };

          setIconFn(UI::m_sphereIcon, 0u);
          setIconFn(UI::m_cubeIcon, 1u);
          setIconFn(UI::m_shaderBallIcon, 2u);

          ImGui::EndGroup();
        }
        ImGui::Spacing();
      }

      auto updateThumbFn = [this, mat]() -> void
      {
        DirectoryEntry dirEnt(mat->GetFile());
        g_app->m_thumbnailManager.UpdateThumbnail(dirEnt);
      };

      if (ImGui::CollapsingHeader("Shaders"))
      {
        ImGui::BeginGroup();
        String vertName;

        ShaderPtr vert = mat->GetVertexShaderVal();
        DecomposePath(vert->GetFile(), nullptr, &vertName, nullptr);

        ImGui::LabelText("##vertex shader: %s", vertName.c_str());
        DropZone(UI::m_codeIcon->m_textureId,
                 vert->GetFile(),
                 [this, mat, &updateThumbFn](const DirectoryEntry& dirEnt) -> void
                 {
                   if (strcmp(dirEnt.m_ext.c_str(), ".shader") != 0)
                   {
                     g_app->SetStatusMsg(g_statusFailed);
                     TK_ERR("Failed. Shader expected.");
                     return;
                   }

                   ShaderPtr shader = GetShaderManager()->Create<Shader>(dirEnt.GetFullPath());
                   shader->Init();
                   mat->SetVertexShaderVal(shader);

                   updateThumbFn();
                 });
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 20.0f);

        ImGui::BeginGroup();

        ShaderPtr frag = mat->GetFragmentShaderVal();
        String fragName;
        DecomposePath(frag->GetFile(), nullptr, &fragName, nullptr);

        ImGui::LabelText("##fragShader fragment shader: %s", fragName.c_str());
        DropZone(UI::m_codeIcon->m_textureId,
                 frag->GetFile(),
                 [this, mat, &updateThumbFn](const DirectoryEntry& dirEnt) -> void
                 {
                   ShaderPtr shader = GetShaderManager()->Create<Shader>(dirEnt.GetFullPath());
                   shader->Init();
                   mat->SetFragmentShaderVal(shader);

                   updateThumbFn();
                 });
        ImGui::EndGroup();
      }

      if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
      {
        auto exposeTextureFn = [this, mat, &updateThumbFn](int textureParamIndex, StringView label) -> void
        {
          String target            = GetPathSeparatorAsStr();
          ParameterVariant& texVar = mat->m_localData[textureParamIndex];
          if (TexturePtr texture = texVar.GetVar<TexturePtr>())
          {
            target = texture->GetFile();
          }

          ImGui::BeginGroup();
          ImGui::PushID(label.data());

          DropZone(
              UI::m_imageIcon->m_textureId,
              target,
              [&texVar, &updateThumbFn](const DirectoryEntry& dirEnt) -> void
              {
                texVar = GetTextureManager()->Create<Texture>(dirEnt.GetFullPath());
                texVar.GetVar<TexturePtr>()->Init();
                updateThumbFn();
              },
              label.data());

          ImGui::PopID();
          ImGui::EndGroup();
        };

        // Use ImGui::Columns for a 2-column layout
        ImGui::Columns(2, "TextureColumns", false); // 2 columns, no border

        // Column 1 (left aligned)
        exposeTextureFn(mat->DiffuseTextureIndex(), "Diffuse");
        exposeTextureFn(mat->NormalTextureIndex(), "Normal");

        ImGui::NextColumn(); // Move to the second column

        // Column 2 (right aligned)
        exposeTextureFn(mat->EmissiveTextureIndex(), "Emissivity");
        exposeTextureFn(mat->MetallicRoughnessTextureIndex(), "Metallic Roughness");

        ImGui::Columns(1); // End columns
      }

      RenderState* renderState = mat->GetRenderState();

      if (ImGui::CollapsingHeader("Render States", ImGuiTreeNodeFlags_DefaultOpen))
      {
        if (mat->GetDiffuseTextureVal() == nullptr)
        {
          Vec3 color = mat->GetColorVal();
          if (ImGui::ColorEdit3("Diffuse Color", &color.x))
          {
            mat->SetColorVal(color);
            updateThumbFn();
          }

          float alpha = mat->GetAlphaVal();
          if (ImGui::DragFloat("Alpha", &alpha, 1.0f / 256.0f, 0.0f, 1.0f))
          {
            mat->SetAlphaVal(alpha);
            updateThumbFn();
          }
        }

        if (mat->GetEmissiveTextureVal() == nullptr)
        {
          Vec3 color = mat->GetEmissiveColorVal();
          if (ImGui::ColorEdit3("Emissivity Color Multiplier##1",
                                &color.x,
                                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_Float))
          {
            mat->SetEmissiveColorVal(color);
            updateThumbFn();
          }
          ImGui::SameLine();
          ImGui::Text("Emissivity Color");
        }

        if (mat->IsPBR() && mat->GetMetallicRoughnessTextureVal() == nullptr)
        {
          float val = mat->GetMetallicVal();
          if (ImGui::DragFloat("Metallic", &val, 0.001f, 0.0f, 1.0f))
          {
            mat->SetMetallicVal(val);
            updateThumbFn();
          }

          val = mat->GetRoughnessVal();
          if (ImGui::DragFloat("Roughness", &val, 0.001f, 0.0f, 1.0f))
          {
            mat->SetRoughnessVal(val);
            updateThumbFn();
          }
        }

        int cullMode = (int) renderState->cullMode;
        if (ImGui::Combo("Cull mode", &cullMode, "Two Sided\0Front\0Back"))
        {
          renderState->cullMode = (CullingType) cullMode;
          updateThumbFn();
        }

        int blendMode = (int) renderState->blendFunction;
        if (ImGui::Combo("Blend mode", &blendMode, "None\0Alpha Blending\0Alpha Mask"))
        {
          mat->SetBlendState((BlendFunction) blendMode);
          updateThumbFn();
        }

        int drawType = DrawTypeToInt(mat->GetRenderState()->drawType);

        if (ImGui::Combo("Draw mode", &drawType, "Triangle\0Line\0Line Strip\0Line Loop\0Point"))
        {
          renderState->drawType = IntToDrawType(drawType);
          updateThumbFn();
        }

        if (mat->IsAlphaMasked())
        {
          float alphaMaskTreshold = renderState->alphaMaskTreshold;
          if (ImGui::DragFloat("Alpha Mask Threshold", &alphaMaskTreshold, 0.001f, 0.0f, 1.0f, "%.3f"))
          {
            mat->SetAlphaMaskThreshold(alphaMaskTreshold);
            updateThumbFn();
          }
        }

        for (int i = 0; i < 3; i++)
        {
          ImGui::Spacing();
        }
      }
    }

    void MaterialView::Show()
    {
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);

      float treeHeight              = glm::min(20.0f + (m_materials.size() * 30.0f), 90.0f);
      int numMaterials              = (int) m_materials.size();

      const auto showMaterialNodeFn = [this](MaterialPtr mat, int i) -> void
      {
        String name;
        DecomposePath(m_materials[i]->GetFile(), nullptr, &name, nullptr);

        bool isSelected          = i == m_currentMaterialIndex;
        ImGuiTreeNodeFlags flags = isSelected * ImGuiTreeNodeFlags_Selected;

        ImGui::TreeNodeEx(name.c_str(), flags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

        if (ImGui::IsItemClicked())
        {
          m_currentMaterialIndex = i;
        }
      };

      if (m_materials.size() == 0)
      {
        ImGui::Text("There is no material selected.");
      }
      else if (numMaterials > 1)
      {
        ImGui::BeginChild("##MultiMaterials", Vec2(0.0f, treeHeight), true);

        if (ImGui::TreeNode("Multi Materials"))
        {
          for (int i = 0; i < m_materials.size(); i++)
          {
            showMaterialNodeFn(m_materials[i], i);
          }
          ImGui::TreePop();
        }
      }

      if (m_materials.size() > 1)
      {
        ImGui::EndChild();
      }

      ImGui::Spacing();

      if (m_materials.size() > 0)
      {
        m_currentMaterialIndex = glm::clamp(m_currentMaterialIndex, 0, (int) (m_materials.size()) - 1);
        ShowMaterial(m_materials[m_currentMaterialIndex]);
      }
    }

    // MaterialWindow
    //////////////////////////////////////////

    TKDefineClass(MaterialWindow, Window);

    MaterialWindow::MaterialWindow()
    {
      m_view               = MakeNewPtr<MaterialView>();
      m_view->m_isTempView = true;
    }

    MaterialWindow::~MaterialWindow() { m_view = nullptr; }

    void MaterialWindow::SetMaterial(MaterialPtr mat) { m_view->SetMaterials({mat}); }

    void MaterialWindow::Show()
    {
      ObjectId wndId  = GetIdVal();
      String strWndId = "Material View##" + std::to_string(wndId);

      ImGuiIO io      = ImGui::GetIO();
      ImGui::SetNextWindowSize(Vec2(400.0f, 700.0f), ImGuiCond_Once);
      ImGui::SetNextWindowPos(Vec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Once, Vec2(0.5f, 0.5f));

      if (ImGui::Begin(strWndId.c_str(), &m_visible))
      {
        HandleStates();
        m_view->Show();
      }
      ImGui::End();
    }

  } // namespace Editor
} // namespace ToolKit