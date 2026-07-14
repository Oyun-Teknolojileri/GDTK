/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include <Types.h>

namespace ToolKit
{
  const String g_workspaceFile("Workspace.settings");
  static const StringView XmlNodePath("path");
  const String g_validLibraryNameRules(
      "Alpha numeric characters and \"_\" are allowed. Do not start with digit. No white space.");

  class Workspace;
  typedef std::shared_ptr<Workspace> WorkspacePtr;
} // namespace ToolKit
