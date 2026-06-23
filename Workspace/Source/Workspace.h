/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "WorkspaceTypes.h"

#include <Serialize.h>
#include <ToolKit.h>
#include <Types.h>

namespace ToolKit
{
  struct Project
  {
    String name;
    String scene;
  };

  class Workspace : public Serializable
  {
   public:
    Workspace();
    void Init();

    // Defaults read / writes to installment directory.
    XmlNode* GetDefaultWorkspaceNode(XmlDocBundle& bundle) const;
    String GetDefaultWorkspace() const;
    bool SetDefaultWorkspace(const String& path);

    // Accessors to workspace
    String GetCodeDirectory() const;   //!< Returns absolute path to the projects' code files.
    String GetConfigDirectory() const; //!< Returns absolute path to project's config files.
    String GetBinPath() const;         //!< Returns absolute path to the compiled binary file for the project.
    String GetPluginDirectory() const; //!< Returns absolute path to projects' plugin directory.
    String GetResourceRoot() const;    //!< Returns absolute path to projects' Resources directory.
    String GetActiveWorkspace() const;
    Project GetActiveProject() const;
    void SetActiveProject(const Project& project);
    void SetScene(const String& scene);

    void RefreshProjects();

    void SerializeEngineSettings(const String& name = "") const;
    void DeSerializeEngineSettings(const String& name = "");

    /**
     * Looks for a valid workspace and scene. Returns true if both exist.
     * If checkProject is true, expect an active project.
     * If reportError is true, reports a console error.
     */
    bool IsWorkspaceSane(bool checkProject, bool reportError) const;

    bool OnNewProject(const String& name);
    bool OnNewPlugin(const String& name);

    static bool DeserializeThemeColors(const String& themeFileName, Vec4Array& outColors);

   protected:
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

   public:
    std::vector<Project> m_projects;

   private:
    String m_activeWorkspace;
    Project m_activeProject;
  };
} // namespace ToolKit
