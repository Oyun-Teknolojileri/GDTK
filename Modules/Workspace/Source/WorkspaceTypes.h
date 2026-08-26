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

  // Workspace is a static library compiled into the Editor and Launcher
  // executables. Its API is exported from the host executable so editor
  // plugins (dlopen'ed into the editor) can consume it. Mirror the
  // TK_EDITOR_API pattern: dllexport for the Workspace / Editor / Launcher
  // builds, dllimport for consumers (plugins), default ELF visibility on
  // Linux where the editor is a single executable with no DLL boundary.
  #ifdef TK_WIN
    #if defined(TK_WORKSPACE_DLL_EXPORT)
      #define TK_WORKSPACE_API __declspec(dllexport)
    #else
      #define TK_WORKSPACE_API __declspec(dllimport)
    #endif
  #else
    #define TK_WORKSPACE_API __attribute__((visibility("default")))
  #endif

  class TK_WORKSPACE_API Workspace;
  typedef std::shared_ptr<Workspace> WorkspacePtr;
} // namespace ToolKit
