/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "RenderDocModule.h"

#include "App.h"
#include "EditorApi.h"
#include "RenderDocCommands.h"
#include "ThirdParty/renderdoc_app.h"
#include "Workspace.h"

#include <FrameCapture.h>
#include <Logger.h>
#include <ToolKit.h>
#include <Util.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

#ifdef TK_WIN
  #define NOMINMAX
  #include <Windows.h>
#else
  #include <dlfcn.h>
#endif

namespace ToolKit
{
  namespace RenderDoc
  {

    namespace
    {

      /**
       * API version asked from RenderDoc. RenderDoc fills the struct up to the
       * version it supports and answers with a newer (backwards compatible)
       * struct when it is newer, so asking for the oldest version that carries
       * everything used here keeps old installs working. 1.4.0 (2019) has frame
       * captures, capture lookup, path templates and discard; the two newer
       * members (ShowReplayUI, SetCaptureTitle) are never touched, because a
       * RenderDoc older than 1.6 would leave them past the end of the struct it
       * returns.
       */
      const RENDERDOC_Version g_requestedApiVersion = eRENDERDOC_API_Version_1_4_0;

      typedef RENDERDOC_API_1_4_0 RenderDocApi;

      /** Frames one capture may span at most, a guard against a typo. */
      const int g_maxCaptureFrames      = 60;

      /** Frames between two request file checks, about four per second at 60 fps. */
      const int g_requestPollInterval   = 8;

      /** File an external tool writes to ask for a capture, inside the capture directory. */
      const char* g_requestFileName     = "request";

      /** File naming the capture that finished last, for tools that poll it. */
      const char* g_lastCaptureFileName = "last_capture.json";

      /**
       * State of the capture tool, and the one place it is allowed to live.
       *
       * A request can arrive from any thread - game code, a plugin, the console
       * - while the frame hooks only run on the render thread, and the console
       * reads the status while a capture is in flight. So the whole state sits
       * behind one mutex and the rule is: take it for the bookkeeping, release
       * it before calling into RenderDoc. Every critical section below is a few
       * field reads; nothing calls out with the lock held, which also means the
       * tool can never deadlock against a host that calls back into it.
       */
      struct CaptureState
      {
        RenderDocApi* api = nullptr; //!< Set once by Init, never re-seated.
        Status status;

        // Queued request, filled by RequestCapture from any thread.
        bool requestQueued = false;
        String queuedLabel;
        int queuedFrames = 1;

        // Capture in progress. The frame hooks run on the render thread; field
        // access still takes the lock so the status copy stays coherent.
        String activeLabel;
        String activePrefix;
        int activeFrameCount = 0;
        int framesLeft       = 0;
        int framesSincePoll  = 0;

        std::mutex mutex;
      };

      /**
       * Process lifetime state for the capture tool. It holds no engine object -
       * strings, an API table owned by the injected library and a mutex - so it
       * does not have to be released before the engine goes away.
       * Shutdown() still drops the handler and the API pointer while
       * everything is alive, which is what the host teardown expects.
       */
      CaptureState& State()
      {
        static CaptureState state;
        return state;
      }

      void LogToConsole(const String& message, LogType type) { Editor::WriteConsoleLine(message, type); }

      String TimeStampNow()
      {
        const std::time_t now = std::time(nullptr);
        std::tm local         = {};
#ifdef TK_WIN
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif

        char buffer[32] = {};
        std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
        return String(buffer);
      }

      /**
       * Turns a caller supplied name into something safe in a file name on every
       * platform: letters, digits, '-' and '_' survive, everything else becomes
       * '_'. An empty name falls back to "capture".
       */
      String SanitizeLabel(const String& label)
      {
        String out;
        out.reserve(label.size() + 1);

        for (char c : label)
        {
          const bool keep =
              (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
          out += keep ? c : '_';
        }

        return out.empty() ? String("capture") : out;
      }

      String EscapeJson(const String& text)
      {
        String out;
        out.reserve(text.size() + 8);

        for (char c : text)
        {
          switch (c)
          {
            case '"':
              out += "\\\"";
              break;
            case '\\':
              out += "\\\\";
              break;
            case '\n':
              out += "\\n";
              break;
            case '\r':
              out += "\\r";
              break;
            case '\t':
              out += "\\t";
              break;
            default:
              if (static_cast<unsigned char>(c) < 0x20)
              {
                char escaped[8] = {};
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(c));
                out += escaped;
              }
              else
              {
                out += c;
              }
              break;
          }
        }

        return out;
      }

      /** Paths end up in JSON and in markdown reports, so they are unixified first. */
      String JsonPath(const String& path)
      {
        String unixified = path;
        UnixifyPath(unixified);
        return EscapeJson(unixified);
      }

      bool WriteTextFile(const String& path, const String& content)
      {
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open())
        {
          return false;
        }

        file << content;
        file.close();
        return true;
      }

      /**
       * Capture directory, resolved per request because the open project can
       * change during a session.
       */
      String ResolveCaptureDir()
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
       * Finds the file RenderDoc just wrote. RenderDoc derives the name from the
       * path template it was given - <template>_frame<N>.rdc, plus a counter
       * when one frame is captured twice - so the prefix of the template, which
       * carries the label and the time stamp, identifies it without depending on
       * how the number is spelled.
       */
      String FindCaptureFile(const String& dir, const String& prefix)
      {
        namespace fs = std::filesystem;

        String found;
        fs::file_time_type foundTime = {};

        std::error_code ec;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec))
        {
          if (!entry.is_regular_file(ec))
          {
            continue;
          }

          const String name = PathToString(entry.path().filename());
          if (!StartsWith(name, prefix) || !EndsWith(name, ".rdc"))
          {
            continue;
          }

          const fs::file_time_type writeTime = entry.last_write_time(ec);
          if (found.empty() || writeTime > foundTime)
          {
            found     = PathToString(entry.path());
            foundTime = writeTime;
          }
        }

        return found;
      }

      /** Writes the metadata an analysis tool needs to put a capture in context. */
      void WriteCaptureMeta(const String& capturePath, const String& label, int frameCount)
      {
        Editor::App* app = GetApp();

        String projectName;
        String sceneName;
        String workspacePath;
        uint frameNumber = 0;

        if (app != nullptr)
        {
          frameNumber = app->m_totalFrameCount;
          if (app->m_workspace != nullptr)
          {
            const Project project = app->m_workspace->GetActiveProject();
            projectName           = project.name;
            sceneName             = project.scene;
            workspacePath         = app->m_workspace->GetActiveWorkspace();
          }
        }

        String json;
        json += "{\n";
        json += "  \"capture\": \"" + JsonPath(capturePath) + "\",\n";
        json += "  \"label\": \"" + EscapeJson(label) + "\",\n";
        json += "  \"frameCount\": " + std::to_string(frameCount) + ",\n";
        json += "  \"editorFrame\": " + std::to_string(frameNumber) + ",\n";
        json += "  \"graphicsApi\": \"" + String(TKVulkan ? "Vulkan" : "OpenGL") + "\",\n";
        json += "  \"engineVersion\": \"" + EscapeJson(TKVersionStr) + "\",\n";
        json += String("  \"config\": \"") + (TKDebug ? "Debug" : "Develop") + "\",\n";
        json += "  \"workspace\": \"" + JsonPath(workspacePath) + "\",\n";
        json += "  \"project\": \"" + EscapeJson(projectName) + "\",\n";
        json += "  \"scene\": \"" + EscapeJson(sceneName) + "\",\n";
        json += "  \"capturedAt\": " + std::to_string(static_cast<long long>(std::time(nullptr))) + "\n";
        json += "}\n";

        String dir;
        String name;
        String ext;
        DecomposePath(capturePath, &dir, &name, &ext);

        // The sidecar travels with the capture, so an old file keeps its context.
        WriteTextFile(ConcatPaths({dir, name + ".json"}), json);

        // And a single well known file points at whichever capture finished last,
        // which is what a tool polls instead of listing the directory.
        WriteTextFile(ConcatPaths({ResolveCaptureDir(), g_lastCaptureFileName}), json);
      }

      void* FindRenderDocModule()
      {
#ifdef TK_WIN
        return static_cast<void*>(GetModuleHandleA("renderdoc.dll"));
#else
        // RTLD_NOLOAD only asks whether the library is already mapped, it never
        // loads anything: the editor does not bring RenderDoc into its own
        // process, the process has to have been started through RenderDoc.
        return dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
#endif
      }

      String GetModulePath(void* module)
      {
        if (module == nullptr)
        {
          return String();
        }

#ifdef TK_WIN
        char buffer[MAX_PATH] = {};
        const DWORD length    = GetModuleFileNameA(static_cast<HMODULE>(module), buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
          return String();
        }
        return String(buffer);
#else
        Dl_info info = {};
        if (dladdr(module, &info) == 0 || info.dli_fname == nullptr)
        {
          return String();
        }
        return String(info.dli_fname);
#endif
      }

    } // namespace

    // Consumes a request file an external tool dropped into the capture
    // directory. Defined below; BeginFrame() is the only caller.
    void PollRequestFile();

    bool Init()
    {
      if (State().api != nullptr)
      {
        return true;
      }

      // The commands exist whether or not RenderDoc is in the process: a build
      // that cannot capture should still be able to say why.
      RegisterEditorContributions();

      void* module = FindRenderDocModule();
      if (module == nullptr)
      {
        // The normal case: the editor was started on its own. Requests stay
        // no-ops and the status command explains why.
        std::lock_guard<std::mutex> lock(State().mutex);
        State().status.m_available = false;
        State().status.m_lastError = "RenderDoc is not loaded in this process.";
        return false;
      }

      const String modulePath = GetModulePath(module);

#ifdef TK_WIN
      pRENDERDOC_GetAPI getAPI =
          reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(static_cast<HMODULE>(module), "RENDERDOC_GetAPI"));
#else
      pRENDERDOC_GetAPI getAPI = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(module, "RENDERDOC_GetAPI"));
#endif

      String error;
      RenderDocApi* api = nullptr;

      if (getAPI == nullptr)
      {
        error = "The RenderDoc library does not export RENDERDOC_GetAPI.";
      }
      else
      {
        void* apiTable = nullptr;
        if (getAPI(g_requestedApiVersion, &apiTable) == 0 || apiTable == nullptr)
        {
          error = "RenderDoc refused API version 1.4.0. Update RenderDoc.";
        }
        else
        {
          api = static_cast<RenderDocApi*>(apiTable);
        }
      }

      if (api == nullptr)
      {
        {
          std::lock_guard<std::mutex> lock(State().mutex);
          State().status.m_available  = false;
          State().status.m_modulePath = modulePath;
          State().status.m_lastError  = error;
        }

        TK_ERR("%s", error.c_str());
        return false;
      }

      int major = 0;
      int minor = 0;
      int patch = 0;
      api->GetAPIVersion(&major, &minor, &patch);

      const String captureDir = ResolveCaptureDir();

      {
        std::lock_guard<std::mutex> lock(State().mutex);
        State().api                 = api;
        State().status.m_available  = true;
        State().status.m_apiMajor   = major;
        State().status.m_apiMinor   = minor;
        State().status.m_apiPatch   = patch;
        State().status.m_modulePath = modulePath;
        State().status.m_captureDir = captureDir;
        State().status.m_lastError.clear();
      }

      // Game code and plugins reach the capture tool through the engine, so they
      // never link an editor symbol and never name RenderDoc.
      SetFrameCaptureRequestFn([](const String& label, int frameCount) -> bool
                               { return RequestCapture(label, frameCount); });

      TK_LOG("RenderDoc %d.%d.%d attached, captures go to %s", major, minor, patch, captureDir.c_str());
      return true;
    }

    void Shutdown()
    {
      // Drop the engine hook first: a request that arrives while the editor
      // tears down must not reach a half dismantled capture state.
      SetFrameCaptureRequestFn(nullptr);

      // The commands go with the module: a console that outlives it must not
      // keep a pointer into code that is no longer there.
      UnregisterEditorContributions();

      RenderDocApi* api = nullptr;
      bool capturing    = false;

      {
        std::lock_guard<std::mutex> lock(State().mutex);
        api                   = State().api;
        capturing             = State().status.m_capturing;

        State().api           = nullptr;
        State().status        = Status();
        State().requestQueued = false;
        State().queuedLabel.clear();
        State().activeLabel.clear();
        State().activePrefix.clear();
        State().framesLeft = 0;
      }

      // A capture still open when the editor closes would be written from the
      // middle of a frame, so it is thrown away. Outside the lock, like every
      // other RenderDoc call.
      if (api != nullptr && capturing)
      {
        api->DiscardFrameCapture(nullptr, nullptr);
      }
    }

    Status GetStatus()
    {
      Status status;
      {
        std::lock_guard<std::mutex> lock(State().mutex);
        status = State().status;
        if (State().requestQueued)
        {
          status.m_requestPending = true;
          status.m_pendingFrames  = State().queuedFrames;
        }
      }

      if (status.m_captureDir.empty())
      {
        status.m_captureDir = ResolveCaptureDir();
      }

      return status;
    }

    bool IsAvailable() { return State().api != nullptr; }

    bool RequestCapture(const String& label, int frameCount)
    {
      if (State().api == nullptr)
      {
        return false;
      }

      if (frameCount < 1)
      {
        frameCount = 1;
      }
      else if (frameCount > g_maxCaptureFrames)
      {
        frameCount = g_maxCaptureFrames;
      }

      std::lock_guard<std::mutex> lock(State().mutex);
      if (State().requestQueued || State().status.m_capturing)
      {
        State().status.m_lastError = "A capture is already queued or running.";
        return false;
      }

      State().requestQueued = true;
      State().queuedLabel   = label.empty() ? String("capture") : label;
      State().queuedFrames  = frameCount;
      State().status.m_lastError.clear();
      return true;
    }

    void BeginFrame()
    {
      if (State().api == nullptr)
      {
        return;
      }

      // A request file is the only way in for a tool that cannot call into the
      // editor, so it is polled right before the queue is looked at.
      PollRequestFile();

      String label;
      String prefix;
      int frameCount          = 1;
      RenderDocApi* api       = nullptr;

      const String captureDir = ResolveCaptureDir();

      {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (!State().requestQueued || State().status.m_capturing)
        {
          return;
        }

        label                 = State().queuedLabel;
        frameCount            = State().queuedFrames;
        State().requestQueued = false;
        State().queuedLabel.clear();

        prefix                         = SanitizeLabel(label) + "_" + TimeStampNow();

        State().activeLabel            = label;
        State().activePrefix           = prefix;
        State().activeFrameCount       = frameCount;
        State().framesLeft             = frameCount;

        State().status.m_capturing     = true;
        State().status.m_captureDir    = captureDir;
        State().status.m_pendingFrames = frameCount;
        api                            = State().api;
      }

      std::error_code ec;
      std::filesystem::create_directories(captureDir, ec);
      if (ec)
      {
        TK_WRN("Capture directory '%s' could not be created: %s", captureDir.c_str(), ec.message().c_str());
      }

      // Unique per capture, so the file RenderDoc writes can be located
      // afterwards without guessing how it numbers frames.
      const String pathTemplate = ConcatPaths({captureDir, prefix});
      api->SetCaptureFilePathTemplate(pathTemplate.c_str());
      api->StartFrameCapture(nullptr, nullptr);
    }

    void EndFrame()
    {
      if (State().api == nullptr)
      {
        return;
      }

      RenderDocApi* api = nullptr;
      String label;
      String prefix;
      String captureDir;
      int frameCount = 0;

      {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (!State().status.m_capturing)
        {
          return;
        }

        if (State().framesLeft > 1)
        {
          --State().framesLeft;
          State().status.m_pendingFrames = State().framesLeft;
          return;
        }

        api                            = State().api;
        label                          = State().activeLabel;
        prefix                         = State().activePrefix;
        captureDir                     = State().status.m_captureDir;
        frameCount                     = State().activeFrameCount;

        State().status.m_capturing     = false;
        State().status.m_pendingFrames = 0;
        State().framesLeft             = 0;
      }

      const uint32_t result = api->EndFrameCapture(nullptr, nullptr);

      if (result == 0)
      {
        const String error = "RenderDoc refused the capture. Was a frame presented inside it?";
        {
          std::lock_guard<std::mutex> lock(State().mutex);
          State().status.m_lastError = error;
        }

        LogToConsole(error, LogType::Error);
        return;
      }

      const String capturePath = FindCaptureFile(captureDir, prefix);
      if (capturePath.empty())
      {
        const String error = "RenderDoc reported a capture but no file appeared in " + captureDir;
        {
          std::lock_guard<std::mutex> lock(State().mutex);
          State().status.m_lastError = error;
        }

        LogToConsole(error, LogType::Error);
        return;
      }

      {
        std::lock_guard<std::mutex> lock(State().mutex);
        State().status.m_lastCapturePath = capturePath;
        ++State().status.m_captureCount;
        State().status.m_lastError.clear();
        State().activeLabel.clear();
        State().activePrefix.clear();
      }

      WriteCaptureMeta(capturePath, label, frameCount);

      String report = capturePath;
      UnixifyPath(report);
      LogToConsole("Capture written: " + report, LogType::Success);
    }

    void PollRequestFile()
    {
      if (State().api == nullptr)
      {
        return;
      }

      {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (++State().framesSincePoll < g_requestPollInterval)
        {
          return;
        }
        State().framesSincePoll = 0;
      }

      const String requestPath = ConcatPaths({ResolveCaptureDir(), g_requestFileName});
      if (!CheckSystemFile(requestPath))
      {
        return;
      }

      String content;
      {
        std::ifstream file(requestPath, std::ios::in);
        if (file.is_open())
        {
          content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
      }

      // Removed before acting on it: a request file that survived a rejected
      // capture would fire again on every poll.
      std::error_code ec;
      std::filesystem::remove(requestPath, ec);

      content = Trim(content);
      if (content.empty())
      {
        return;
      }

      StringArray words;
      Split(content, " ", words);

      int frameCount = 1;
      if (words.size() > 1)
      {
        frameCount = std::atoi(words[1].c_str());
      }

      if (!RequestCapture(words.front(), frameCount))
      {
        String error;
        {
          std::lock_guard<std::mutex> lock(State().mutex);
          error = State().status.m_lastError;
        }
        LogToConsole("Capture request '" + content + "' rejected: " + error, LogType::Error);
      }
    }

    bool OpenLastCapture()
    {
      RenderDocApi* api = nullptr;
      String lastCapture;

      {
        std::lock_guard<std::mutex> lock(State().mutex);
        api         = State().api;
        lastCapture = State().status.m_lastCapturePath;
      }

      if (api == nullptr)
      {
        return false;
      }

      const char* captureFile = lastCapture.empty() ? nullptr : lastCapture.c_str();

      // 1 connects the UI back to this process, so the capture opens with the
      // running frame next to it instead of as a standalone file.
      return api->LaunchReplayUI(1, captureFile) != 0;
    }

    String GetCaptureDir() { return ResolveCaptureDir(); }

  } // namespace RenderDoc
} // namespace ToolKit
