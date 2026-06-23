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

    struct DynamicMenu
    {
      String MenuName;
      std::vector<std::pair<String, String>> MenuEntries; //!< Class - Name pairs.
      DynamicMenuPtrArray SubMenuArray;                   //!< SubMenu array of the menu.

      void AddSubMenuUnique(DynamicMenuPtr subMenu);
    };

    /** Creates the object using Class name. Lambda is responsible for constructing and placing the object. */
    extern void ShowDynamicMenu(DynamicMenuPtr parentMenu, const std::function<void(const StringView&)>& constructor);

    /** Construct dynamic object menu from the menu-class descriptor. */
    extern void ConstructDynamicMenu(StringArray menuDescriptors, DynamicMenuPtrArray& menuArray);

  } // namespace Editor
} // namespace ToolKit