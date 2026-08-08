/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Workspace.h"

#include <FileManager.h>
#include <Scene.h>
#include <Util.h>

#include <fstream>
#include <sstream>

namespace ToolKit
{
  static constexpr StringView XmlNodeWorkspace = "Workspace";
  static constexpr StringView XmlNodeProject   = "Project";
  static constexpr StringView XmlNodeScene     = "scene";

  Workspace::Workspace() {}

  void Workspace::Init()
  {
    m_activeWorkspace = GetDefaultWorkspace();
    DeSerialize(SerializationFileInfo(), nullptr);
  }

  XmlNode* Workspace::GetDefaultWorkspaceNode(XmlDocBundle& bundle) const
  {
    String settingsFile = ConcatPaths({ConfigPath(), g_workspaceFile});

    if (CheckFile(settingsFile))
    {
      XmlFilePtr lclFile    = GetFileManager()->GetXmlFile(settingsFile.c_str());
      XmlDocumentPtr lclDoc = MakeNewPtr<XmlDocument>();
      lclDoc->parse<0>(lclFile->data());

      bundle.doc       = lclDoc;
      bundle.file      = lclFile;

      StringArray path = {XmlNodeSettings.data(), XmlNodeWorkspace.data()};
      return Query(lclDoc.get(), path);
    }

    return nullptr;
  }

  String Workspace::GetDefaultWorkspace() const
  {
    String path;
    XmlDocBundle docBundle;
    if (XmlNode* node = GetDefaultWorkspaceNode(docBundle))
    {
      String foundPath;
      ReadAttr(node, XmlNodePath.data(), foundPath);
      NormalizePathInplace(foundPath);
      if (CheckFile(foundPath))
      {
        path = foundPath;
      }
    }

    return path;
  }

  bool Workspace::SetDefaultWorkspace(const String& path)
  {
    XmlDocBundle docBundle;
    if (XmlNode* node = GetDefaultWorkspaceNode(docBundle))
    {
      std::ofstream file;
      String settingsPath = ConcatPaths({ConfigPath(), g_workspaceFile});

      file.open(settingsPath.c_str(), std::ios::out);
      if (file.is_open())
      {
        m_activeWorkspace = path;
        RefreshProjects();
        if (XmlAttribute* attr = node->first_attribute(XmlNodePath.data()))
        {
          attr->value(docBundle.doc->allocate_string(path.c_str(), 0));
        }
        else
        {
          WriteAttr(node, docBundle.doc.get(), XmlNodePath.data(), path);
        }

        String xml;
        rapidxml::print(std::back_inserter(xml), *docBundle.doc.get());

        file << xml;
        file.close();

        return true;
      }
    }

    return false;
  }

  String Workspace::GetCodeDirectory() const
  {
    if (m_activeProject.name.empty())
    {
      TK_ERR("Code directory does not exist. There is no active project.");
    }

    String codePath = ConcatPaths({GetActiveWorkspace(), m_activeProject.name, "Codes"});
    return codePath;
  }

  String Workspace::GetConfigDirectory() const
  {
    if (m_activeProject.name.empty())
    {
      return m_activeWorkspace;
    }

    return ConcatPaths({m_activeWorkspace, m_activeProject.name, "Config"});
  }

  String Workspace::GetBinPath() const
  {
    String codePath   = GetCodeDirectory();
    String pluginPath = ConcatPaths({codePath, "Bin", m_activeProject.name});

    return pluginPath;
  }

  String Workspace::GetPluginDirectory() const
  {
    return ConcatPaths({m_activeWorkspace, m_activeProject.name, "Plugins"});
  }

  String Workspace::GetResourceRoot() const
  {
    if (m_activeProject.name.empty())
    {
      return m_activeWorkspace;
    }

    return ConcatPaths({m_activeWorkspace, m_activeProject.name, "Resources"});
  }

  String Workspace::GetActiveWorkspace() const { return m_activeWorkspace; }

  Project Workspace::GetActiveProject() const { return m_activeProject; }

  void Workspace::SetActiveProject(const Project& project)
  {
    m_activeProject                     = project;
    Main::GetInstance()->m_resourceRoot = GetResourceRoot();
  }

  void Workspace::SetScene(const String& scene) { m_activeProject.scene = scene; }

  void Workspace::RefreshProjects()
  {
    m_projects.clear();
    for (const std::filesystem::directory_entry& dir : std::filesystem::directory_iterator(m_activeWorkspace))
    {
      if (dir.is_directory())
      {
        String dirPath = PathToString(dir.path());
        if (dirPath.find(".git") != String::npos)
        {
          // Skip git directory.
          continue;
        }

        String resourcesPath = ConcatPaths({dirPath, "Resources"});
        String codesPath     = ConcatPaths({dirPath, "Codes"});

        // Skip directory if it doesn't have folders: Resources, Codes
        if (!std::filesystem::directory_entry(resourcesPath).is_directory() ||
            !std::filesystem::directory_entry(codesPath).is_directory())
        {
          continue;
        }

        const StringArray requiredResourceFolders = {"Materials", "Meshes", "Scenes", "Textures"};
        bool foundAllRequiredFolders              = true;
        for (uint i = 0; i < requiredResourceFolders.size(); i++)
        {
          String checkDir = ConcatPaths({resourcesPath, requiredResourceFolders[i]});
          if (!(std::filesystem::directory_entry(checkDir).is_directory()))
          {
            foundAllRequiredFolders = false;
            break;
          }
        }

        if (!foundAllRequiredFolders)
        {
          continue;
        }

        // Don't show hidden folders
        String dirName = PathToString(dir.path().filename());
        if (dirName.size() > 1 && dirName[0] != '.')
        {
          Project project = {dirName, ""};
          m_projects.push_back(project);
        }
      }
    }
  }

  XmlNode* Workspace::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    std::ofstream file;
    String fileName = ConcatPaths({ConfigPath(), g_workspaceFile});

    file.open(fileName.c_str(), std::ios::out);
    if (file.is_open())
    {
      XmlDocument* lclDoc = new XmlDocument();
      XmlNode* settings   = CreateXmlNode(lclDoc, XmlNodeSettings.data());
      WriteAttr(settings, lclDoc, XmlVersion, TKVersionStr);

      XmlNode* setNode = CreateXmlNode(lclDoc, XmlNodeWorkspace.data(), settings);
      WriteAttr(setNode, lclDoc, XmlNodePath.data(), m_activeWorkspace);

      setNode = CreateXmlNode(lclDoc, XmlNodeProject.data(), settings);
      WriteAttr(setNode, lclDoc, XmlNodeName.data(), m_activeProject.name);

      ScenePtr currentScene = GetSceneManager()->GetCurrentScene();
      if (currentScene)
      {
        String sceneFile = currentScene->GetFile();
        if (!sceneFile.empty())
        {
          String scenePath = GetRelativeResourcePath(sceneFile);
          // Only save if the path is under a known resource root
          // (GetRelativeResourcePath returns a different string on success).
          if (scenePath != sceneFile)
          {
            WriteAttr(setNode, lclDoc, XmlNodeScene.data(), scenePath);
          }
        }
      }

      std::string xml;
      rapidxml::print(std::back_inserter(xml), *lclDoc);

      file << xml;
      file.close();
      lclDoc->clear();
      SafeDel(lclDoc);
    }

    return nullptr;
  }

  void Workspace::SerializeEngineSettings(const String& name) const
  {
    String settingsFile = name.empty() ? ConcatPaths({GetConfigDirectory(), "Engine.settings"})
                                       : ConcatPaths({GetConfigDirectory(), name});
    GetEngineSettings().Save(settingsFile);
  }

  void Workspace::DeSerializeEngineSettings(const String& name)
  {
    String settingsFile = name.empty() ? ConcatPaths({GetConfigDirectory(), "Engine.settings"})
                                       : ConcatPaths({GetConfigDirectory(), name});

    // Search for settings file,
    // if its not exist pull default Engine.settings file from appdata
    if (!CheckSystemFile(settingsFile))
    {
      settingsFile = ConcatPaths({ConfigPath(), "Engine.settings"});
    }

    GetEngineSettings().Load(settingsFile);
  }

  XmlNode* Workspace::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    String settingsFile   = ConcatPaths({ConfigPath(), g_workspaceFile});

    XmlFilePtr lclFile    = MakeNewPtr<XmlFile>(settingsFile.c_str());
    XmlDocumentPtr lclDoc = MakeNewPtr<XmlDocument>();
    lclDoc->parse<0>(lclFile->data());

    if (XmlNode* settings = lclDoc->first_node(XmlNodeSettings.data()))
    {
      ReadAttr(settings, XmlVersion.data(), m_version);

      if (XmlNode* setNode = settings->first_node(XmlNodeWorkspace.data()))
      {
        String foundWorkspacePath;
        ReadAttr(setNode, XmlNodePath.data(), foundWorkspacePath);
        NormalizePathInplace(foundWorkspacePath);
        if (CheckFile(foundWorkspacePath))
        {
          m_activeWorkspace = foundWorkspacePath;
        }
      }

      if (m_activeWorkspace.length())
      {
        RefreshProjects();

        String projectName, sceneName;
        if (XmlNode* setNode = settings->first_node(XmlNodeProject.data()))
        {
          ReadAttr(setNode, XmlNodeName.data(), projectName);
          ReadAttr(setNode, XmlNodeScene.data(), sceneName);
        }

        for (const Project& project : m_projects)
        {
          if (project.name == projectName)
          {
            Project project = {projectName, sceneName};
            SetActiveProject(project);
            break;
          }
        }
      }
    }
    else
    {
      TK_ERR("Workspace.settings file is faulty. Remove %appdata%/ToolKit folder on windows to reset settings.");
      assert(0 && "Workspace.settings file is faulty.");
    }

    DeSerializeEngineSettings();
    return nullptr;
  }

  bool Workspace::IsWorkspaceSane(bool checkProject, bool reportError) const
  {
    if (GetActiveWorkspace().empty())
    {
      if (reportError)
      {
        TK_ERR("No workspace. Can not proceed with operation.");
      }
      return false;
    }

    if (checkProject)
    {
      if (GetActiveProject().name.empty())
      {
        if (reportError)
        {
          TK_ERR("No project. Can not proceed with operation.");
        }
        return false;
      }
    }

    return true;
  }

  void AlterTextContent(std::fstream& fileEditStream, const String& filePath, const String content)
  {
    fileEditStream.open(filePath, std::ios::out | std::ios::trunc);
    if (fileEditStream.is_open())
    {
      fileEditStream << content;
      fileEditStream.close();
    }
  }

  void TemplateUpdate(const String& file, const String& replaceSoruce, const String& replaceTarget)
  {
    std::fstream fileEditStream;
    fileEditStream.open(file, std::ios::in);
    if (fileEditStream.is_open())
    {
      std::stringstream buffer;
      buffer << fileEditStream.rdbuf();
      String content = buffer.str();
      ReplaceFirstStringInPlace(content, replaceSoruce.data(), replaceTarget);
      fileEditStream.close();

      AlterTextContent(fileEditStream, file, content);
    }
  }

  // note: only copy template folder
  bool Workspace::OnNewProject(const String& name)
  {
    if (!IsWorkspaceSane(false, true))
    {
      return false;
    }

    if (!IsValidCppLibraryName(name))
    {
      TK_ERR("Invalid project name: %s.", name.c_str());
      TK_LOG("%s", g_validLibraryNameRules.c_str());
      return false;
    }

    String fullPath = ConcatPaths({GetActiveWorkspace(), name});
    if (CheckFile(fullPath))
    {
      TK_ERR("Project already exist.");
      return false;
    }

    // copy template folder to new workspace
    RecursiveCopyDirectory(ConcatPaths({"..", "Templates", "Game"}),
                           fullPath,
                           {".filters", ".vcxproj", ".user", ".cxx"});

    // Update cmake.
    String currentPath = GetCurrentParentPath();
    String cmakePath   = ConcatPaths({fullPath, "Codes", "CMakeLists.txt"});
    TemplateUpdate(cmakePath, "__projectname__", name);

    return true;
  }

  bool Workspace::OnNewPlugin(const String& name)
  {
    if (!IsWorkspaceSane(true, true))
    {
      return false;
    }

    if (!IsValidCppLibraryName(name))
    {
      TK_ERR("Invalid plugin name: %s.", name.c_str());
      TK_LOG("%s", g_validLibraryNameRules.c_str());
      return false;
    }

    String fullPath = ConcatPaths({GetPluginDirectory(), name});
    if (CheckSystemFile(fullPath))
    {
      TK_ERR("A plugin with the same name already exist in the project.");
      return false;
    }

    // Copy template folder to new project.
    RecursiveCopyDirectory(ConcatPaths({"..", "Templates", "Plugin"}),
                           fullPath,
                           {".filters", ".vcxproj", ".user", ".cxx"});

    // Update cmake.
    String currentPath = PathToString(std::filesystem::current_path().parent_path());
    String cmakePath   = ConcatPaths({fullPath, "Codes", "CMakeLists.txt"});
    TemplateUpdate(cmakePath, "__projectname__", name);

    String pluginSettingsPath = ConcatPaths({fullPath, "Config", "Plugin.settings"});
    TemplateUpdate(pluginSettingsPath, "PluginTemplate", name);

    TK_LOG("A new plugin has been created.");
    return true;
  }

  void Workspace::DeleteProject(const String& path)
  {
    if (path.empty() || !CheckSystemFile(path))
    {
      TK_ERR("Project path does not exist: %s", path.c_str());
      return;
    }

    // Safety check: only delete directories that live inside the active
    // workspace (not some arbitrary system path).
    String workspacePath = GetActiveWorkspace();
    if (workspacePath.empty())
    {
      TK_ERR("No active workspace. Cannot delete project.");
      return;
    }

    if (path.find(workspacePath) != 0)
    {
      TK_ERR("Project path is not inside the active workspace. Refusing to delete.");
      return;
    }

    // Additional safety: the path must contain at least one level deeper
    // than the workspace (the project folder itself).
    String relative = path.substr(workspacePath.size());
    if (relative.empty() || relative == "/" || relative == "\\")
    {
      TK_ERR("Refusing to delete the workspace root.");
      return;
    }

    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec)
    {
      TK_ERR("Failed to delete project: %s", ec.message().c_str());
    }
    else
    {
      TK_LOG("Project deleted: %s", path.c_str());
    }
  }

  bool Workspace::DeserializeThemeColors(const String& themeFileName, Vec4Array& outColors)
  {
    String path = ConcatPaths({ConfigPath(), themeFileName});

    std::ifstream file(path);
    if (!file.is_open())
    {
      return false;
    }

    outColors.clear();

    String line;
    while (std::getline(file, line))
    {
      if (line.empty())
      {
        continue;
      }

      Vec4 col;
      if (sscanf(line.c_str(), "%*s %f,%f,%f,%f", &col.x, &col.y, &col.z, &col.w) == 4)
      {
        outColors.push_back(col);
      }
    }

    return !outColors.empty();
  }

} // namespace ToolKit
