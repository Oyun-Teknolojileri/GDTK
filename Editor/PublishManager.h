/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorTypes.h"

namespace ToolKit
{
  namespace Editor
  {

    enum class PublishConfig
    {
      Debug   = 0, // Debug build
      Develop = 1, // Release build
      Deploy  = 2  // Release build with calling packer
    };

    enum class PublishPlatform
    {
      Web,
      Windows,
      Linux,
      Android,
      GamePlugin,
      EditorPlugin
    };

    enum class AndroidABI
    {
      All,
      ArmeabiV7a,
      Arm64V8a,
      X86,
      X86_64
    };

    enum class MobileOriantation
    {
      Undefined,
      Landscape,
      Portrait
    };

    class TK_EDITOR_API PublishManager
    {
     public:
      void Publish(PublishPlatform platform, PublishConfig publishConfig);
      void Pack();

     private:
      String ConstructPublishArgs(PublishPlatform platform, PublishConfig publishConfig, bool packOnly);

     public:
      TexturePtr m_icon = nullptr;
      String m_appName;
      String m_pluginName;
      bool m_deployAfterBuild = false;
      int m_minSdk            = 27;
      int m_maxSdk            = 32;

      MobileOriantation m_oriantation;
      bool m_isBuilding        = false;
      AndroidABI m_selectedABI = AndroidABI::All;
    };

  } // namespace Editor
} // namespace ToolKit
