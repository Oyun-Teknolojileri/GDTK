/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "RenderDocCommands.h"

#include "EditorApi.h"
#include "RenderDocModule.h"

#include <Util.h>

namespace ToolKit
{
  namespace RenderDoc
  {

    namespace
    {

      /**
       * The module owns its commands end to end: it registers them through the
       * editor API in Init() and takes them back out in Shutdown(), so the
       * editor never carries a line of RenderDoc specific code. Command format:
       *
       *   CaptureFrame <label> [<frames>]
       *   CaptureStatus
       *   CaptureOpen
       */
      void CaptureFrameExec(Editor::TagArgArray tagArgs)
      {
        String label   = "editor";
        int frameCount = 1;

        if (!tagArgs.empty() && tagArgs.front().first.empty() && !tagArgs.front().second.empty())
        {
          label = tagArgs.front().second.front();
          if (tagArgs.front().second.size() > 1)
          {
            frameCount = std::atoi(tagArgs.front().second[1].c_str());
          }
        }

        if (!RequestCapture(label, frameCount))
        {
          const Status status = GetStatus();
          Editor::WriteConsoleLine("Capture failed: " + status.m_lastError, LogType::Error);
          return;
        }

        Editor::WriteConsoleLine("Capture requested: " + label, LogType::Memo);
      }

      void CaptureStatusExec(Editor::TagArgArray tagArgs)
      {
        (void) tagArgs;

        const Status status = GetStatus();

        if (!status.m_available)
        {
          Editor::WriteConsoleLine("RenderDoc: unavailable. " + status.m_lastError, LogType::Warning);
          Editor::WriteConsoleLine("  Start the editor through RenderDoc: renderdoccmd capture -w Editor.exe",
                                   LogType::Memo);
          Editor::WriteConsoleLine("  Or attach to a running editor:      renderdoccmd inject <pid>", LogType::Memo);
          return;
        }

        Editor::WriteConsoleLine(
            Format("RenderDoc: available, API %d.%d.%d", status.m_apiMajor, status.m_apiMinor, status.m_apiPatch),
            LogType::Memo);
        Editor::WriteConsoleLine("  Library:    " + status.m_modulePath, LogType::Memo);
        Editor::WriteConsoleLine("  Directory:  " + status.m_captureDir, LogType::Memo);
        Editor::WriteConsoleLine(Format("  Captures:   %d this session", status.m_captureCount), LogType::Memo);

        if (!status.m_lastCapturePath.empty())
        {
          Editor::WriteConsoleLine("  Last:       " + status.m_lastCapturePath, LogType::Memo);
        }

        if (status.m_capturing)
        {
          Editor::WriteConsoleLine(Format("  Capturing now, %d frame(s) left", status.m_pendingFrames), LogType::Memo);
        }
        else if (!status.m_lastError.empty())
        {
          Editor::WriteConsoleLine("  Last error: " + status.m_lastError, LogType::Warning);
        }
      }

      void CaptureOpenExec(Editor::TagArgArray tagArgs)
      {
        (void) tagArgs;

        if (!OpenLastCapture())
        {
          Editor::WriteConsoleLine("Could not open the RenderDoc UI.", LogType::Error);
        }
      }

    } // namespace

    void RegisterCommands()
    {
      static bool registered = false;
      if (registered)
      {
        return;
      }

      registered = true;
      Editor::RegisterConsoleCommand("CaptureFrame", CaptureFrameExec);
      Editor::RegisterConsoleCommand("CaptureStatus", CaptureStatusExec);
      Editor::RegisterConsoleCommand("CaptureOpen", CaptureOpenExec);
    }

    void UnregisterCommands()
    {
      Editor::UnregisterConsoleCommand("CaptureFrame");
      Editor::UnregisterConsoleCommand("CaptureStatus");
      Editor::UnregisterConsoleCommand("CaptureOpen");
    }

  } // namespace RenderDoc
} // namespace ToolKit
