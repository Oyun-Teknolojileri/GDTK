/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Types.h"
#include "Util.h"

namespace ToolKit
{
  struct NetworkPlayBootManifest
  {
    bool Valid        = false;
    bool Headless     = false;
    bool AutoPlay     = false;
    String ConfigRoot;
    String ResourceRoot;
    String ProjectRoot;
    String WorkspaceRoot;
    String ScenePath;
    String SceneSnapshotPath;
    String TempRoot;
    String LogRoot;
    StringArray RuntimePlugins;
  };

  inline String ReadNetworkPlayManifestAttr(XmlNode* node, const char* name)
  {
    if (node == nullptr)
    {
      return {};
    }

    if (XmlAttribute* attr = node->first_attribute(name))
    {
      return attr->value();
    }

    return {};
  }

  inline bool ReadNetworkPlayManifestBoolAttr(XmlNode* node, const char* name, bool fallback = false)
  {
    const String value = ReadNetworkPlayManifestAttr(node, name);
    if (value.empty())
    {
      return fallback;
    }

    return value == "1" || value == "true" || value == "True";
  }

  inline XmlNode* FindNetworkPlayManifestRoot(XmlDocument& doc)
  {
    XmlNode* root = doc.first_node("NetworkPlayInstance");
    if (root == nullptr)
    {
      root = doc.first_node("NetworkPlayManifest");
    }

    return root;
  }

  inline bool ParseNetworkPlayManifestNode(XmlNode* root, NetworkPlayBootManifest& manifest)
  {
    if (root == nullptr)
    {
      return false;
    }

    manifest = {};
    manifest.ConfigRoot = ReadNetworkPlayManifestAttr(root, "configRoot");
    manifest.ResourceRoot = ReadNetworkPlayManifestAttr(root, "resourceRoot");
    manifest.ProjectRoot = ReadNetworkPlayManifestAttr(root, "projectRoot");
    manifest.WorkspaceRoot = ReadNetworkPlayManifestAttr(root, "workspaceRoot");
    manifest.ScenePath = ReadNetworkPlayManifestAttr(root, "scenePath");
    manifest.SceneSnapshotPath = ReadNetworkPlayManifestAttr(root, "sceneSnapshotPath");
    manifest.TempRoot = ReadNetworkPlayManifestAttr(root, "tempRoot");
    manifest.LogRoot = ReadNetworkPlayManifestAttr(root, "logRoot");
    manifest.Headless = ReadNetworkPlayManifestBoolAttr(root, "headless", false);
    manifest.AutoPlay = ReadNetworkPlayManifestBoolAttr(root, "autoPlay", false);

    if (XmlNode* runtimePluginsNode = root->first_node("RuntimePlugins"))
    {
      for (XmlNode* pluginNode = runtimePluginsNode->first_node("Plugin"); pluginNode != nullptr;
           pluginNode = pluginNode->next_sibling("Plugin"))
      {
        const String pluginName = ReadNetworkPlayManifestAttr(pluginNode, "name");
        if (!pluginName.empty())
        {
          manifest.RuntimePlugins.push_back(pluginName);
        }
      }
    }

    if (manifest.ResourceRoot.empty() && !manifest.ProjectRoot.empty())
    {
      manifest.ResourceRoot = ConcatPaths({manifest.ProjectRoot, "Resources"});
    }
    if (manifest.ConfigRoot.empty() && !manifest.ProjectRoot.empty())
    {
      manifest.ConfigRoot = ConcatPaths({manifest.ProjectRoot, "Config"});
    }

    manifest.Valid = true;
    return true;
  }

  inline bool ParseNetworkPlayManifestXml(StringView manifestXml, NetworkPlayBootManifest& manifest)
  {
    if (manifestXml.empty())
    {
      return false;
    }

    try
    {
      String xml(manifestXml);
      xml.push_back('\0');

      XmlDocument doc;
      doc.parse<0>(xml.data());
      return ParseNetworkPlayManifestNode(FindNetworkPlayManifestRoot(doc), manifest);
    }
    catch (...)
    {
      return false;
    }
  }

  inline bool ParseNetworkPlayManifestFile(StringView manifestPath, NetworkPlayBootManifest& manifest)
  {
    const String path(manifestPath);
    if (path.empty() || !CheckSystemFile(path))
    {
      return false;
    }

    try
    {
      XmlFile file(path.c_str());
      XmlDocument doc;
      doc.parse<0>(file.data());
      return ParseNetworkPlayManifestNode(FindNetworkPlayManifestRoot(doc), manifest);
    }
    catch (...)
    {
      return false;
    }
  }

  inline String FindNetworkPlayManifestPath(int argc, char* argv[])
  {
    constexpr const char* prefix = "-networkPlayManifest=";

    for (int i = 0; i < argc; ++i)
    {
      const String arg = argv[i];
      if (arg.rfind(prefix, 0) == 0)
      {
        return arg.substr(String(prefix).size());
      }
    }

    return {};
  }
} // namespace ToolKit
