/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Window.h"

#include <set>

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API EngineSettingsWindow : public Window
    {
     public:
      TKDeclareClass(EngineSettingsWindow, Window);

      /** Tabs of the window, in the order the tab bar draws them. The value is what gets stored. */
      enum class Tab
      {
        Graphics = 0,
        Shadows,
        PostProcessing
      };

      EngineSettingsWindow();
      virtual ~EngineSettingsWindow();
      void Show() override;

     protected:
      void ShowPostProcessingTab(bool select);
      void ShowGraphicsTab(bool select);
      void ShowShadowsTab(bool select);

      /**
       * Draws a collapsing section and keeps its open state. ImGui holds that state for the session
       * only, so the window stores the sections that are open and hands them back the first time a
       * section is drawn after a load.
       */
      bool ShowSection(const char* label);

      /** Stores the tab that is in front and the sections that are open. */
      XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
      XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

     private:
      bool m_showLoadWindow   = false; // Footer load dialog state

      /** Tab that is in front. */
      Tab m_activeTab         = Tab::Graphics;

      /** True until the stored tab has been selected in the tab bar once. */
      bool m_restoreActiveTab = false;

      /** Sections (collapsing headers) that are open. The set is ordered, so the file is stable. */
      std::set<String> m_openSections;

      /** Open sections whose stored state has already been handed to ImGui. */
      std::set<String> m_restoredSections;
    };

  } // namespace Editor
} // namespace ToolKit