/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "DirectoryEntry.h"
#include "Types.h"
#include "UI.h"

namespace ToolKit
{
  namespace Editor
  {

    enum class ViewType
    {
      Entity,
      CustomData,
      Component,
      Material,
      Mesh,
      Prefab,
      ViewCount
    };

    class TK_EDITOR_API View
    {
     public:
      View(const StringView viewName);
      virtual ~View();
      virtual void Show() = 0;

      /**
       * A drop zone to drop mesh, material or texture files and perform an action based on dropped file.
       * Widget shows an icon either a thumbnail of associated file or fallbackIcon, title (drop name) and action to
       * perform when an entry is dropped (drop action).
       * DropZone can be disabled by setting isEditable to false.
       */
      static void DropZone(uint fallbackIcon,
                           const String& file,
                           std::function<void(DirectoryEntry& entry)> dropAction,
                           const String& dropName = "",
                           bool isEditable        = true);

      /** A drop zone which can be placed inside a tree node. */
      static void DropSubZone(const String& title,
                              uint fallbackIcon,
                              const String& file,
                              std::function<void(const DirectoryEntry& entry)> dropAction,
                              bool isEditable = true);

      static bool IsTextInputFinalized();

     public:
      EntityWeakPtr m_entity;
      int m_viewID         = 0;
      TexturePtr m_viewIcn = nullptr;
      StringView m_fontIcon;
      const StringView m_viewName;
    };

  } // namespace Editor
} // namespace ToolKit