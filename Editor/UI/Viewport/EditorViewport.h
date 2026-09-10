/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "DirectoryEntry.h"
#include "EditorTypes.h"
#include "Window.h"

#include <Viewport.h>

namespace ToolKit
{
  namespace Editor
  {

    enum class CameraAlignment
    {
      Free,
      Top,
      Front,
      Left,
      User
    };

    /**
     * Editor viewport class. Inherits from Viewport and Window, and integrates editor-specific functionality.
     */
    class TK_EDITOR_API EditorViewport : public Viewport, public Window
    {
     public:
      TKDeclareClass(EditorViewport, Window);

      EditorViewport();
      virtual ~EditorViewport();

      virtual void Init(Vec2 size);

      // Window Overrides.
      void Show() override;
      void Update(float deltaTime) override;
      bool IsViewportQueriable() const;
      void DispatchSignals() const override;

      // Viewport overrides.
      void OnResizeContentArea(float width, float height) override;
      virtual void ResizeWindow(uint width, uint height);

      /**
       * Drops the entity and debug objects cached for an in flight asset drag and drop.
       * Must be called while the engine is still alive (App::Destroy), otherwise a drag that
       * was interrupted by shutdown keeps engine resources alive until process exit.
       */
      static void ReleaseDragDropState();

      // Editor functions
      void GetContentAreaScreenCoordinates(Vec2* min, Vec2* max) const;
      void SetCamera(CameraPtr cam) override;
      virtual void ResetCameraToDefault(); //!< Reset camera settings to default.

     protected:
      XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
      XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

      virtual void UpdateContentArea();
      virtual void UpdateWindow();
      virtual void DrawCommands();

      /**
       * Resolves the viewport's render target into an ImGui ready texture handle.
       * An MSAA render target is resolved to its single sample attachment, which is the only
       * one ImGui can sample. Falls back to the black texture when the render target is missing
       * or when its resolved attachment is not available yet.
       * @return Texture id, convertible to ImTextureID via ConvertUIntImGuiTexture.
       */
      uint64 GetImGuiTextureId() const;

      virtual void HandleDrop();
      virtual void DrawOverlays();
      virtual void ComitResize();
      virtual void UpdateSnaps();

      // Mods.
      void FpsNavigationMod(float deltaTime);
      void OrbitPanMod(float deltaTime);
      void AdjustZoom(float delta) override;

     private:
      void LoadDragMesh(bool& meshLoaded,
                        DirectoryEntry dragEntry,
                        EntityPtr* dwMesh,
                        LineBatchPtr* boundingBox,
                        EditorScenePtr currScene);

      Vec3 CalculateDragMeshPosition(bool& meshLoaded,
                                     EditorScenePtr currScene,
                                     EntityPtr dwMesh,
                                     LineBatchPtr* boundingBox);

      void HandleDropMesh(bool& meshLoaded,
                          bool& meshAddedToScene,
                          EditorScenePtr currScene,
                          EntityPtr* dwMesh,
                          LineBatchPtr* boundingBox);

      // Command palette. Space opens a command line popup over the viewport,
      // a lightweight alternative to the console window.
      bool ShouldOpenCommandPalette() const;
      void OpenCommandPalette();
      void CloseCommandPalette();
      void ShowCommandPalette();
      void RunCommandPalette();
      int CommandPaletteTextCallback(ImGuiInputTextCallbackData* data);
      StringArray FilterCommandPaletteMatches(const String& token) const;

     public:
      // Window properties.
      static std::vector<class OverlayUI*> m_overlays;
      bool m_mouseOverOverlay           = false;
      CameraAlignment m_cameraAlignment = CameraAlignment::Free;
      int m_additionalWindowFlags       = 0;
      bool m_orbitLock                  = false;
      Vec3 m_snapDeltas; // X: Translation, Y: Rotation, Z: Scale

      // UI Draw commands.
      std::vector<std::function<void(ImDrawList*)>> m_drawCommands;

      EditorRendererPtr m_editorRenderer;

     protected:
      Vec2 m_contentAreaMin;
      Vec2 m_contentAreaMax;
      IVec2 m_mousePosBegin;
      bool m_needsResize          = false;
      bool m_mouseOverContentArea = false;

     private:
      // States.
      bool m_relMouseModBegin = true;

      // Command palette states.
      bool m_commandPaletteOpen        = false;
      bool m_commandPaletteNeedsFocus  = false;
      bool m_commandPaletteScrollToSel = false;
      String m_commandPaletteText;
      String m_commandPaletteLastToken;
      int m_commandPaletteSelection = 0;
    };

  } // namespace Editor
} // namespace ToolKit
