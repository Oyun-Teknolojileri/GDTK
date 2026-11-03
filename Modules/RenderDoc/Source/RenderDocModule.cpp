/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "RenderDocModule.h"

#include "App.h"
#include "RenderDocCommands.h"
#include "Workspace.h"

#include <Util.h>

#include <map>

namespace ToolKit
{
  namespace RenderDoc
  {

    namespace
    {

      /**
       * Captures go next to the project so the analysis tool and a human find
       * them, not into a global folder. Resolved per capture because switching
       * projects mid session has to move the target with it.
       */
      String EditorCaptureRoot()
      {
        Editor::App* app = GetApp();
        if (app != nullptr && app->m_workspace != nullptr)
        {
          const String workspace = app->m_workspace->GetActiveWorkspace();
          const Project project  = app->m_workspace->GetActiveProject();
          if (!workspace.empty() && !project.name.empty())
          {
            return ConcatPaths({workspace, project.name, "Captures"});
          }
        }

        return ConcatPaths({ConfigPath(), "Captures"});
      }

      /**
       * The engine core keeps the sidecar free of host types, so the editor's
       * context travels as plain key value pairs.
       */
      std::map<String, String> EditorCaptureContext()
      {
        std::map<String, String> context;

        Editor::App* app = GetApp();
        if (app == nullptr || app->m_workspace == nullptr)
        {
          return context;
        }

        const Project project = app->m_workspace->GetActiveProject();
        context["workspace"]  = app->m_workspace->GetActiveWorkspace();
        context["project"]    = project.name;
        context["scene"]      = project.scene;

        return context;
      }

    } // namespace

    bool Init()
    {
      const bool available = RenderDocCapture::Init(EditorCaptureRoot, EditorCaptureContext);

      // Registered whether or not RenderDoc is in the process: a build that
      // cannot capture should still be able to say why.
      RegisterEditorContributions();

      return available;
    }

    void Shutdown()
    {
      UnregisterEditorContributions();
      RenderDocCapture::Shutdown();
    }

    void BeginFrame() { RenderDocCapture::BeginFrame(); }

    void EndFrame() { RenderDocCapture::EndFrame(); }

    bool IsAvailable() { return RenderDocCapture::IsAvailable(); }

    Status GetStatus() { return RenderDocCapture::GetStatus(); }

    bool RequestCapture(const String& label, int frameCount)
    {
      return RenderDocCapture::RequestCapture(label, frameCount);
    }

    bool OpenLastCapture() { return RenderDocCapture::OpenLastCapture(); }

  } // namespace RenderDoc
} // namespace ToolKit