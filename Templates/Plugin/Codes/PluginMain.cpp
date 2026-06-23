/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "PluginMain.h"

ToolKit::Editor::PluginMain Self;

extern "C" TK_PLUGIN_API ToolKit::Plugin* TK_STDCAL GetInstance() { return &Self; }

namespace ToolKit
{
  namespace Editor
  {

    void PluginMain::Init(Main* master) { Main::SetProxy(master); }

    void PluginMain::Destroy() {}

    void PluginMain::Frame(float deltaTime) {}

    void PluginMain::OnLoad(XmlDocumentPtr state) {}

    void PluginMain::OnUnload(XmlDocumentPtr state) {}

    void PluginMain::OnPlay() {}

    void PluginMain::OnPause() {}

    void PluginMain::OnResume() {}

    void PluginMain::OnStop() {}

  } // namespace Editor
} // namespace ToolKit
