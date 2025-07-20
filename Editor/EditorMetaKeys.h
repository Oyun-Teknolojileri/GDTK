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

    /**
     * @brief Adds a new menu entry to Editor for a registered class.
     * External classes with this meta key, causes to add new classes to the Editor's menu system.
     * The classes are added according to the specified menu descriptor pattern.
     * 
     * Menu descriptor pattern is a string that follows the pattern "Menu/SubMenu/Class:Name".
     * The "Menu/SubMenu" defines the location in the target menu where the class will appear.
     * The "Class" is the identifier used to construct the object, typically obtained from T::StaticClass()->Name.
     * The "Name" is the display name for the object in the menu.
     */
    constexpr StringView EntityMenuMetaKey = "EntityMenuMetaKey";

  } // namespace Editor
} // namespace ToolKit
