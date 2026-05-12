/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "NetworkPlayBootManifest.h"

namespace ToolKit
{
  inline bool ResolveNetworkPlayHeadlessMode(bool cliHeadless,
                                             const NetworkPlayBootManifest& manifest)
  {
    return cliHeadless || (manifest.Valid && manifest.Headless);
  }

  inline bool ShouldAutoPlayNetworkPlayBoot(const NetworkPlayBootManifest& manifest)
  {
    return !manifest.Valid || manifest.AutoPlay;
  }

  inline String ResolveNetworkPlayScenePath(const NetworkPlayBootManifest& manifest)
  {
    if (manifest.Valid && !manifest.SceneSnapshotPath.empty())
    {
      return manifest.SceneSnapshotPath;
    }

    return manifest.ScenePath;
  }
} // namespace ToolKit
