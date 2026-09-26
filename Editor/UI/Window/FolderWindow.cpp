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

    namespace
    {
      // The resource root a stored folder path was written against. A browser lists the project
      // resources next to the engine ones, so a restored path has to say which of the two it
      // belongs to: the same relative path means a different folder under each root.
      const String g_projectRootTag = "Project";
      const String g_engineRootTag  = "Engine";

      /**
       * Returns the tag of the resource root the folder lives under, or an empty string when it is
       * under neither the project nor the engine resources. Folder paths are absolute, so the
       * comparison is too.
       */
      String GetFolderRootTag(const String& folder)
      {
        const String sep         = GetPathSeparatorAsStr();

        const String projectRoot = ToAbsolutePath(ResourcePath());
        if (folder == projectRoot || StartsWith(folder, projectRoot + sep))
        {
          return g_projectRootTag;
        }

        const String engineRoot = ToAbsolutePath(DefaultPath());
        if (folder == engineRoot || StartsWith(folder, engineRoot + sep))
        {
          return g_engineRootTag;
        }

        return String();
      }

      /** Returns the absolute path of the resource root a tag stands for. */
      String GetFolderRootPath(const String& rootTag)
      {
        if (rootTag == g_projectRootTag)
        {
          return ToAbsolutePath(ResourcePath());
        }

        if (rootTag == g_engineRootTag)
        {
          return ToAbsolutePath(DefaultPath());
        }

        return String();
      }

      /**
       * Splits a folder path into the tag of the resource root it belongs to and its path relative
       * to that root, which is how the editor stores a folder. A folder that is under neither root
       * keeps its absolute path and gets an empty tag.
       */
      void SplitFolderPath(const String& folder, String& rootTag, String& relativePath)
      {
        rootTag      = GetFolderRootTag(folder);
        relativePath = folder;

        if (rootTag.empty())
        {
          return;
        }

        const String root = GetFolderRootPath(rootTag);
        relativePath      = folder.length() > root.length() ? folder.substr(root.length() + 1) : String();
      }

      /**
       * Rebuilds a folder path from a root tag and a path relative to it. An empty tag means the
       * stored path was absolute, an unknown tag means the entry cannot be resolved at all.
       */
      String JoinFolderPath(const String& rootTag, const String& relativePath)
      {
        if (rootTag.empty())
        {
          return relativePath;
        }

        const String root = GetFolderRootPath(rootTag);
        if (root.empty())
        {
          return String();
        }

        // An empty relative path is the root of that resource tree itself.
        return relativePath.empty() ? root : ConcatPaths({root, relativePath});
      }
    } // namespace

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

      // Whether a tree node is expanded is ImGui state that no layout file holds for a plain tree
      // node, so the window carries it (see SerializeImp). The first time a node is drawn after a
      // load, the stored state is handed to ImGui; from then on the user owns it. A node that is
      // inside a collapsed parent is not drawn yet, so its stored state waits for the parent.
      if (node.childs.size() > 0 && m_restoredFolders.find(node.path) == m_restoredFolders.end())
      {
        m_restoredFolders.insert(node.path);
        ImGui::SetNextItemOpen(m_openFolders.find(node.path) != m_openFolders.end(), ImGuiCond_Always);
      }

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
        const bool open = ImGui::TreeNodeEx(stdId.c_str(), nodeFlags, nodeHeader.c_str());

        if (open)
        {
          // Track the state the tree is left in.
          m_openFolders.insert(node.path);

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
        else
        {
          // A collapsed folder drops out, so collapsing is what gets saved for it.
          m_openFolders.erase(node.path);
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
      // No fixed size: ImGui keeps the size and the position the layout file holds for a window it
      // already knows, and fits a window that is opened for the first time to its content. The
      // constraints only keep that first fit usable.
      ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 240.0f), ImVec2(TK_FLT_MAX, TK_FLT_MAX));
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

    void FolderWindow::RefreshAllAssetBrowsers(bool includeTrees)
    {
      for (FolderWindow* window : GetApp()->GetAssetBrowsers())
      {
        window->SetViewsDirty();

        if (includeTrees)
        {
          window->SetTreeDirty();
        }
      }
    }

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

      // The index is written for an editor that predates the path below. It is not a reliable way
      // to find the folder again -- it depends on the order the file system was iterated in, which
      // differs between sessions -- so the path is what a restore uses.
      WriteAttr(folder, doc, "activeFolder", std::to_string(m_activeFolder));
      WriteAttr(folder, doc, "showStructure", std::to_string(m_showStructure));

      // The folder the browser was left on. The tab bar, the tabs it shows and the tab in front
      // all follow from this path, so storing it is what makes a browser come back as it was.
      String rootTag, folderPath;
      if (m_activeFolder >= 0 && m_activeFolder < (int) m_entries.size())
      {
        SplitFolderPath(m_entries[m_activeFolder].GetPath(), rootTag, folderPath);
      }

      WriteAttr(folder, doc, "activeFolderRoot", rootTag);
      WriteAttr(folder, doc, "activeFolderPath", folderPath);

      // The folders the tree is open on. Which tree node is expanded is ImGui state that no layout
      // file carries, so without this the tree comes back fully collapsed. The set is ordered, so
      // the same tree always produces the same file.
      for (const String& openFolder : m_openFolders)
      {
        // A folder that is gone does not get an entry again.
        if (!std::filesystem::is_directory(openFolder))
        {
          continue;
        }

        String openRootTag, openFolderPath;
        SplitFolderPath(openFolder, openRootTag, openFolderPath);

        XmlNode* openNode = CreateXmlNode(doc, "OpenFolder", folder);
        WriteAttr(openNode, doc, "root", openRootTag);
        WriteAttr(openNode, doc, "path", openFolderPath);
      }

      return folder;
    }

    XmlNode* FolderWindow::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      // The settings of this window hang under the <Window> node that Window::DeSerializeImp
      // returns, while the node the caller passes in is the <Object> element wrapping it. Reading
      // them from the latter found nothing, so the folder the browser was left on never came back.
      XmlNode* wndNode          = Window::DeSerializeImp(info, parent);
      XmlNode* folderWindowNode = wndNode != nullptr ? wndNode->first_node("FolderWindow") : nullptr;

      // The index is what an editor that predates the path below wrote, and it is all a settings
      // file from that version has to offer.
      int legacyActiveFolder    = 0;
      String activePath;
      if (folderWindowNode != nullptr)
      {
        ReadAttr(folderWindowNode, "activeFolder", legacyActiveFolder);
        ReadAttr(folderWindowNode, "showStructure", m_showStructure);

        String rootTag, folderPath;
        ReadAttr(folderWindowNode, "activeFolderRoot", rootTag);
        ReadAttr(folderWindowNode, "activeFolderPath", folderPath);
        activePath = JoinFolderPath(rootTag, folderPath);

        m_openFolders.clear();
        m_restoredFolders.clear();
        for (XmlNode* openNode = folderWindowNode->first_node("OpenFolder"); openNode;
             openNode          = openNode->next_sibling("OpenFolder"))
        {
          String openRootTag, openFolderPath;
          ReadAttr(openNode, "root", openRootTag);
          ReadAttr(openNode, "path", openFolderPath);

          const String openFolder = JoinFolderPath(openRootTag, openFolderPath);
          if (!openFolder.empty())
          {
            m_openFolders.insert(openFolder);
          }
        }
      }

      // Iterate() resets the active folder, so the remembered one is applied once the entries of
      // the project and of the engine tree exist and can be looked up by path.
      Iterate(ResourcePath(), true);

      // A folder that was deleted while the editor was closed, or a path that no longer belongs to
      // the workspace, leaves the browser on the resources root instead of opening a tab for a
      // folder that is not there.
      if (!activePath.empty() && !GetFolderRootTag(activePath).empty() && std::filesystem::is_directory(activePath))
      {
        FolderView::SelectFolder(this, activePath);
      }
      else
      {
        SetActiveView(legacyActiveFolder);
      }

      return nullptr;
    }

  } // namespace Editor
} // namespace ToolKit
