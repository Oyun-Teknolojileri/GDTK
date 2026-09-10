/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "FolderWindow.h"

#include "App.h"

#include <Animation.h>
#include <Audio.h>
#include <Material.h>
#include <Mesh.h>
#include <Scene.h>
#include <Shader.h>
#include <Texture.h>
#include <ToolKit.h>

namespace ToolKit
{
  namespace Editor
  {

    String GetRootPath(const String& folder)
    {
      static std::unordered_set<String> rootMap = {"Fonts", "Materials", "Meshes", "Scenes", "Shaders", "Textures"};

      String path                               = folder;
      String subFolder {};
      // traverse parent paths and search a root folder
      while (path.size() > 0ull)
      {
        subFolder.clear();

        while (path.size() > 0ull)
        {
          // pop last character
          char c = path.back();
          path.erase(path.end() - 1ull);

          if (c == '\\' || c == '/')
          {
            break;
          }
          // push character to subFolder
          subFolder.push_back(c);
        }
        // reverse because we pushed reversely
        std::reverse(subFolder.begin(), subFolder.end());
        // if we found a root folder return this path
        if (rootMap.count(subFolder) > 0)
        {
          return ConcatPaths({path, subFolder});
        }
      }
      return DefaultPath();
    }

    TKDefineClass(FolderWindow, Window);

    FolderWindow::FolderWindow() {}

    FolderWindow::~FolderWindow()
    {
      // The file operation state points into this window's folder views, which are gone by the
      // time this destructor returns.
      FolderView::ReleaseFileOperationState();
    }

    // destroy old one and create new tree
    void FolderWindow::ReconstructFolderTree()
    {
      // A pending rebuild request is satisfied by this one.
      m_treeDirty = false;

      m_folderNodes.clear();
      m_treeRoots.clear();

      // Engine resources.
      int engineRoot   = CreateTreeRec(DefaultPath());

      // Project resources. Without an active workspace ResourcePath() falls back
      // to DefaultPath(), adding it again would list the engine folders twice.
      int resourceRoot = -1;
      if (ResourcePath() != DefaultPath())
      {
        resourceRoot = CreateTreeRec(ResourcePath());
      }

      // Project resources are shown above the engine ones. A root that isn't a
      // readable directory is left out of the tree completely.
      if (resourceRoot != -1)
      {
        m_treeRoots.push_back(resourceRoot);
      }

      if (engineRoot != -1)
      {
        m_treeRoots.push_back(engineRoot);
      }
    }

    void FolderWindow::IterateFolders(bool includeEngine)
    {
      // Keep showing the folder the user is in. Iterate() rebuilds the entries
      // and resets the active folder, so it has to be restored by path.
      String activePath;
      if (FolderView* activeView = GetActiveView())
      {
        activePath = activeView->GetPath();
      }

      Iterate(ResourcePath(), true, includeEngine);

      if (!activePath.empty())
      {
        int indx = Exist(activePath);
        if (indx != -1)
        {
          m_activeFolder = indx;
        }
      }
    }

    // Adds the folder hierarchy under the given path, returns its root's node index.
    // Returns -1 when the path isn't a readable directory.
    int FolderWindow::CreateTreeRec(const String& path)
    {
      std::error_code pathEc;
      if (!std::filesystem::is_directory(path, pathEc) || pathEc)
      {
        TK_ERR("FolderWindow::CreateTreeRec: '%s' is not a directory (%s).",
               path.c_str(),
               pathEc ? pathEc.message().c_str() : "path missing");
        return -1;
      }

      String folderName = GetFileName(path);
      int index         = (int) m_folderNodes.size();
      m_folderNodes.emplace_back(index, ToAbsolutePath(path), folderName);

      // Non-throwing iterator, a single unreadable entry won't kill the tree.
      std::error_code iterEc;
      for (auto it = std::filesystem::directory_iterator(path, iterEc); !iterEc && it != std::filesystem::end(it);
           it.increment(iterEc))
      {
        const std::filesystem::directory_entry& directory = *it;

        std::error_code entEc;
        if (!directory.is_directory(entEc) || entEc)
        {
          continue;
        }

        String subDir = NormalizePath(PathToString(directory.path()));
        int childIdx  = CreateTreeRec(subDir);
        if (childIdx != -1)
        {
          m_folderNodes[index].childs.push_back(childIdx);
        }
      }

      if (iterEc)
      {
        TK_ERR("FolderWindow::CreateTreeRec failed to iterate '%s': %s", path.c_str(), iterEc.message().c_str());
      }

      return index;
    }

    extern FolderView* g_dragBeginView;

    void FolderWindow::DrawTreeRec(int index, float depth)
    {
      // A node is missing when the tree is empty or when the folder vanished
      // with the last rebuild. Guards against a stale index as well.
      if (index < 0 || index >= (int) m_folderNodes.size())
      {
        return;
      }

      FolderNode& node        = m_folderNodes[index];
      FolderView* activeView  = GetActiveView();
      IntArray ascendantViews = GetAscendants();

      // Check all ascendants to set their icons open.
      bool folderOpen         = false;
      for (int i : ascendantViews)
      {
        folderOpen |= node.path == m_entries[i].GetPath();
      }

      if (activeView != nullptr)
      {
        folderOpen |= node.path == activeView->GetRoot(); // Include current root as well.
      }

      String icon             = folderOpen ? ICON_FA_FOLDER_OPEN_A : ICON_FA_FOLDER_A;
      String nodeHeader       = icon + ICON_SPACE + node.name;
      float headerLen         = ImGui::CalcTextSize(nodeHeader.c_str()).x;
      headerLen              += (depth * 20.0f) + 70.0f; // depth padding + UI start padding

      m_maxTreeNodeWidth      = glm::max(headerLen, m_maxTreeNodeWidth);

      const auto onClickedFn  = [&]() -> void
      {
        // SelectFolder creates a view for the clicked folder when it has none
        // yet. Folders that came to existence after the last full iteration
        // (an import target, a new directory, a paste) have a tree node but no
        // view, without this a click on them would silently do nothing.
        FolderView::SelectFolder(this, node.path);
      };

      const auto acceptDrop = [&]() -> void
      {
        if (ImGui::BeginDragDropTarget())
        {
          if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BrowserDragZone"))
          {
            if (g_dragBeginView != nullptr)
            {
              g_dragBeginView->DropFiles(node.path);
            }
          }
          ImGui::EndDragDropTarget();
        }
      };

      ImGuiTreeNodeFlags nodeFlags = g_treeNodeFlags;
      // Ids are derived from the folder path instead of the node index, so the
      // expanded / collapsed state survives a rebuild of the tree.
      String stdId                 = "##" + node.path;

      if (node.childs.size() == 0)
      {
        nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        if (ImGui::TreeNodeEx(stdId.c_str(), nodeFlags, nodeHeader.c_str()))
        {
          if (ImGui::IsItemClicked())
          {
            onClickedFn();
          }
        }
        acceptDrop();
      }
      else
      {
        if (ImGui::TreeNodeEx(stdId.c_str(), nodeFlags, nodeHeader.c_str()))
        {
          if (ImGui::IsItemClicked())
          {
            onClickedFn();
          }

          for (int i = 0; i < node.childs.size(); ++i)
          {
            DrawTreeRec(node.childs[i], depth + 1.0f);
          }
          ImGui::TreePop();
        }
        acceptDrop();
      }
    }

    void FolderWindow::ShowFolderTree()
    {
      // Show Resource folder structure.
      ImGui::PushID("##FolderStructure");
      ImGui::BeginGroup();

      ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
      ImGui::TextUnformatted("Resources");

      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_ARROW_LEFT))
      {
        m_showStructure = !m_showStructure;
      }

      ImGui::BeginChild("##Folders", ImVec2(m_maxTreeNodeWidth, 0.0f), true);

      // reset tree node default size
      m_maxTreeNodeWidth = 160.0f;
      // draw tree of folders
      for (int root : m_treeRoots)
      {
        DrawTreeRec(root, 0.0f);
      }

      ImGui::EndChild();

      ImGui::PopStyleVar();
      ImGui::EndGroup();
      ImGui::PopID();
    }

    StringArray FolderWindow::GetFolderChain(const String& path) const
    {
      const String sep          = GetPathSeparatorAsStr();
      const String resourceRoot = ToAbsolutePath(ResourcePath());
      const String engineRoot   = ToAbsolutePath(DefaultPath());

      // Find the tree root the folder belongs to. The project resources come
      // first, they can be nested inside the engine ones.
      String rootPath           = engineRoot;
      if (resourceRoot != engineRoot && (path == resourceRoot || StartsWith(path, resourceRoot + sep)))
      {
        rootPath = resourceRoot;
      }

      StringArray chainPaths;
      chainPaths.push_back(rootPath);

      if (path.size() > rootPath.size() && StartsWith(path, rootPath))
      {
        StringArray subDirs;
        Split(path.substr(rootPath.size()), sep, subDirs);

        String chainPath = rootPath;
        for (const String& subDir : subDirs)
        {
          chainPath = ConcatPaths({chainPath, subDir});
          chainPaths.push_back(chainPath);
        }
      }

      return chainPaths;
    }

    IntArray FolderWindow::GetAscendants()
    {
      // Find all the folders from the tree root down to the active folder. Every
      // one of them keeps its tab, so the parents stay reachable while the user
      // goes deeper into the hierarchy.
      FolderView* activeFolder = GetActiveView();
      if (activeFolder == nullptr)
      {
        return {};
      }

      IntArray views;
      for (const String& chainPath : GetFolderChain(activeFolder->GetPath()))
      {
        int indx = Exist(chainPath);
        if (indx != -1)
        {
          views.push_back(indx);
        }
      }

      return views;
    }

    IntArray FolderWindow::GetSiblings()
    {
      IntArray siblings;
      if (FolderView* view = GetActiveView())
      {
        String path = view->GetPath();

        StringArray target;
        Split(path, GetPathSeparatorAsStr(), target);

        size_t lastSep  = path.find_last_of(GetPathSeparator());
        String checkStr = path.substr(0, lastSep);

        for (int i = 0; i < m_entries.size(); i++)
        {
          FolderView& candidate = m_entries[i];

          // If candidate and active folder shares same root.
          String candidatePath  = candidate.GetPath();
          if (candidatePath.find(checkStr) != String::npos)
          {
            // And splits have same number of entry
            StringArray src;
            Split(candidatePath, GetPathSeparatorAsStr(), src);
            if (src.size() == target.size())
            {
              // Than they are siblings.
              siblings.push_back(i);
            }
          }
        }
      }

      return siblings;
    }

    void FolderWindow::UpdateCurrentRoot()
    {
      for (int i = 0; i < (int) m_entries.size(); i++)
      {
        FolderView& view = m_entries[i];
        view.m_currRoot  = false;

        if (view.m_folderIndex == m_activeFolder)
        {
          // Find the root that active folder is belong to and set it as current.
          String rootStr = view.GetRoot();
          for (int ii = 0; ii < (int) m_entries.size(); ii++)
          {
            FolderView& rootCandidate = m_entries[ii];
            if (rootCandidate.GetPath() == rootStr)
            {
              rootCandidate.m_currRoot = true;
              return;
            }
          }
        }
      }
    }

    void FolderWindow::Show()
    {
      ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_Once);
      if (ImGui::Begin(m_name.c_str(), &m_visible))
      {
        HandleStates();

        if (!GetApp()->m_workspace->GetActiveWorkspace().empty() &&
            GetApp()->m_workspace->GetActiveProject().name.empty())
        {
          ImGui::Text("Load a project.");
          ImGui::End();
          return;
        }

        // Rebuild the hierarchy before it is drawn. The rebuild is deferred up
        // to this point because the request can come from a tree node drop or
        // from a folder view while it is being drawn, and replacing the nodes
        // during either of those would invalidate the references in use.
        if (m_treeDirty)
        {
          m_treeDirty = false;
          ReconstructFolderTree();
        }

        if (m_showStructure)
        {
          ShowFolderTree();
        }
        else
        {
          if (ImGui::Button(ICON_FA_ARROW_RIGHT))
          {
            m_showStructure = !m_showStructure;
          }
        }

        ImGui::SameLine();

        UpdateCurrentRoot();

        ImGui::PushID("##FolderContent");
        ImGui::BeginGroup();

        if (ImGui::BeginTabBar("Folders", ImGuiTabBarFlags_None))
        {
          // Draw each tab.
          IntArray views = GetAscendants();
          for (int i = 0; i < (int) views.size(); i++)
          {
            int folderIndex  = views[i];
            FolderView& view = m_entries[folderIndex];
            view.m_active    = m_activeFolder == folderIndex; // Set activation.
            view.Show();
            view.m_visible = view.m_active; // Set visibility due imgui altering tab activity.
          }
          ImGui::EndTabBar();
        }

        ImGui::EndGroup();
        ImGui::PopID();
      }

      ImGui::End();
    }

    void FolderWindow::Iterate(const String& path, bool clear, bool addEngine)
    {
      String resourceRoot = ResourcePath();
      char pathSep        = GetPathSeparator();
      int baseCount       = CountChar(resourceRoot, pathSep);

      // Resolve to an absolute path. Workspace paths stored without a leading
      // slash (e.g. coming from Editor.settings) are otherwise resolved against
      // the editor's CWD and silently miss the real directory.
      String absPath      = ToAbsolutePath(path);

      // Guard: directory_iterator throws on a missing/non-directory path.
      // Bail with a clear log instead of crashing the editor. This has to run
      // before the entries are cleared, an early out afterwards would leave the
      // window without entries while the active folder still points into them.
      std::error_code pathEc;
      if (!std::filesystem::is_directory(absPath, pathEc) || pathEc)
      {
        TK_ERR("FolderWindow::Iterate: '%s' (resolved to '%s') is not a directory (%s).",
               path.c_str(),
               absPath.c_str(),
               pathEc ? pathEc.message().c_str() : "path missing");
        return;
      }

      if (clear)
      {
        m_activeFolder = 0;
        m_entries.clear();
        ReconstructFolderTree();

        // The resources folder itself is part of the hierarchy, the tab bar of
        // every folder below it starts with it.
        FolderView rootView(this);
        rootView.SetPath(absPath);
        rootView.m_root = true;
        rootView.Iterate();
        AddEntry(rootView);
      }

      // Non-throwing iterator: a single unreadable entry won't kill the loop.
      std::error_code iterEc;
      for (auto it = std::filesystem::directory_iterator(absPath, iterEc); !iterEc && it != std::filesystem::end(it);
           it.increment(iterEc))
      {
        const std::filesystem::directory_entry& entry = *it;

        std::error_code entEc;
        if (!entry.is_directory(entEc) || entEc)
        {
          continue;
        }

        FolderView view(this);
        String childPath = NormalizePath(PathToString(entry.path()));
        view.m_root      = CountChar(childPath, pathSep) == baseCount + 1;

        view.SetPath(childPath);
        if (!view.m_folder.compare("Engine"))
        {
          continue;
        }

        view.Iterate();
        AddEntry(view);
        Iterate(view.GetPath(), false, false);
      }
      if (iterEc)
      {
        TK_ERR("FolderWindow failed to iterate '%s': %s", absPath.c_str(), iterEc.message().c_str());
      }

      if (addEngine)
      {
        // Engine folder
        FolderView view(this);
        view.SetPath(DefaultPath());

        view.Iterate();
        AddEntry(view);
        Iterate(view.GetPath(), false, false);
      }
    }

    void FolderWindow::UpdateContent()
    {
      for (FolderView& view : m_entries)
      {
        view.Iterate();
      }
    }

    void FolderWindow::AddEntry(FolderView& view)
    {
      if (Exist(view.GetPath()) == -1)
      {
        view.m_folderIndex = (int) m_entries.size();
        m_entries.push_back(view);
      }
    }

    void FolderWindow::SetViewsDirty()
    {
      for (FolderView& view : m_entries)
      {
        view.SetDirty();
      }
    }

    void FolderWindow::SetTreeDirty() { m_treeDirty = true; }

    FolderView& FolderWindow::GetView(int indx) { return m_entries[indx]; }

    FolderView* FolderWindow::GetActiveView()
    {
      // m_activeFolder can outlive the entry it points to, a failed iteration
      // leaves the entries empty for example.
      if (m_activeFolder < 0 || m_activeFolder >= (int) m_entries.size())
      {
        return nullptr;
      }

      FolderView& rootView = GetView(m_activeFolder);
      return &rootView;
    }

    void FolderWindow::SetActiveView(int index)
    {
      if (index >= 0 && index < (int) m_entries.size())
      {
        m_activeFolder = index;
      }
    }

    void FolderWindow::SetActiveView(FolderView* view)
    {
      int indx = Exist(view->GetPath());
      SetActiveView(indx);
    }

    int FolderWindow::Exist(const String& path)
    {
      for (size_t i = 0; i < m_entries.size(); i++)
      {
        if (m_entries[i].GetPath() == path)
        {
          return (int) i;
        }
      }

      return -1;
    }

    bool FolderWindow::GetFileEntry(const String& fullPath, DirectoryEntry& entry)
    {
      String path, name, ext;
      DecomposePath(fullPath, &path, &name, &ext);
      int viewIndx = Exist(path);
      if (viewIndx != -1)
      {
        int dirEntry = m_entries[viewIndx].Exist(name, ext);
        if (dirEntry != -1)
        {
          entry = m_entries[viewIndx].m_entries[dirEntry];
          return true;
        }
      }

      return false;
    }

    XmlNode* FolderWindow::SerializeImp(XmlDocument* doc, XmlNode* parent) const
    {
      XmlNode* wndNode = Window::SerializeImp(doc, parent);
      XmlNode* folder  = CreateXmlNode(doc, "FolderWindow", wndNode);

      WriteAttr(folder, doc, "activeFolder", std::to_string(m_activeFolder));
      WriteAttr(folder, doc, "showStructure", std::to_string(m_showStructure));

      return folder;
    }

    XmlNode* FolderWindow::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      Window::DeSerializeImp(info, parent);
      if (XmlNode* node = parent->first_node("FolderWindow"))
      {
        ReadAttr(node, "activeFolder", m_activeFolder);
        ReadAttr(node, "showStructure", m_showStructure);
      }

      Iterate(ResourcePath(), true);

      return nullptr;
    }

  } // namespace Editor
} // namespace ToolKit
