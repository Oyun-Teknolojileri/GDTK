/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#define GLAD_GL_IMPLEMENTATION
#include "TKOpenGL.h"

#include "Types.h"

#ifdef TK_WEB
  #include <emscripten.h>
  #include <emscripten/html5.h>
#endif

#include "DebugNew.h"

namespace ToolKit
{

  TKGL_FramebufferTexture2DMultisample tk_glFramebufferTexture2DMultisampleEXT = nullptr;

  TKGL_RenderbufferStorageMultisample tk_glRenderbufferStorageMultisampleEXT   = nullptr;

  TKGL_InsertEventMarker tk_glInsertEventMarkerEXT                             = nullptr;

  TKGL_PopGroupMarker tk_glPopGroupMarkerEXT                                   = nullptr;

  TKGL_PushGroupMarker tk_glPushGroupMarkerEXT                                 = nullptr;

  TKGL_GetObjectLabel tk_glGetObjectLabelEXT                                   = nullptr;

  TKGL_LabelObject tk_glLabelObjectEXT                                         = nullptr;

  TKGL_InvalidateFramebuffer tk_glInvalidateFramebufferEXT                     = nullptr;

  int TK_GL_EXT_texture_filter_anisotropic                                     = 0;

  int TK_GL_OES_texture_float_linear                                           = 0;

  int TK_GL_EXT_depth_clamp                                                    = 0;

  namespace
  {
    /** Kept so the program binary extension can be resolved on demand, after the host loaded GL. */
    typedef void* (*TKGLProcLoader)(const char*);
    TKGLProcLoader g_glGetProcAddress = nullptr;
  } // namespace

  void* TKGetGlProcAddress(const char* name)
  {
    if (g_glGetProcAddress == nullptr || name == nullptr)
    {
      return nullptr;
    }
    return g_glGetProcAddress(name);
  }

  void LoadGlFunctions(void* glGetProcAddres)
  {
    g_glGetProcAddress = (TKGLProcLoader) glGetProcAddres;
    // glad fills in the GL function pointers. Without this every gl*
    // call is a NULL function pointer and the first glEnable inside
    // GLBackend::InitBackend segfaults. Windows and Linux use the same
    // glad build so they share the load + extension-probe block;
    // Android uses a manual glLoader path (its GLES surface differs)
    // and the Web uses Emscripten's own WebGL headers.
#if defined(TK_WIN) || defined(TK_LINUX)
    gladLoadGL((GLADloadfunc) glGetProcAddres);

  #ifdef GL_EXT_multisampled_render_to_texture
    if (GLAD_GL_EXT_multisampled_render_to_texture == 1)
    {
      tk_glFramebufferTexture2DMultisampleEXT = glad_glFramebufferTexture2DMultisampleEXT;
      tk_glRenderbufferStorageMultisampleEXT  = glad_glRenderbufferStorageMultisampleEXT;
    }
  #endif

  #ifdef GL_EXT_texture_filter_anisotropic
    TK_GL_EXT_texture_filter_anisotropic = GLAD_GL_EXT_texture_filter_anisotropic;
  #endif

  #ifdef GL_OES_texture_float_linear
    TK_GL_OES_texture_float_linear = GLAD_GL_OES_texture_float_linear;
  #endif

  #ifdef GL_DEPTH_CLAMP
    TK_GL_EXT_depth_clamp = 1;
  #endif

  #ifdef GL_EXT_debug_marker

    tk_glInsertEventMarkerEXT = glad_glInsertEventMarkerEXT;
    tk_glPopGroupMarkerEXT    = glad_glPopGroupMarkerEXT;
    tk_glPushGroupMarkerEXT   = glad_glPushGroupMarkerEXT;

  #endif

  #ifdef GL_EXT_debug_label

    tk_glLabelObjectEXT    = glad_glLabelObjectEXT;
    tk_glGetObjectLabelEXT = glad_glGetObjectLabelEXT;

  #endif

  #ifdef GL_ARB_invalidate_subdata
    tk_glInvalidateFramebufferEXT = glad_glInvalidateFramebuffer;
  #endif

#endif

#ifdef TK_ANDROID

    typedef void* (*GL_PROC_ADDR)(const char*);
    GL_PROC_ADDR glLoader = (GL_PROC_ADDR) glGetProcAddres;

    tk_glRenderbufferStorageMultisampleEXT =
        (TKGL_RenderbufferStorageMultisample) glLoader("glRenderbufferStorageMultisampleEXT");
    tk_glFramebufferTexture2DMultisampleEXT =
        (TKGL_FramebufferTexture2DMultisample) glLoader("glFramebufferTexture2DMultisampleEXT");

    tk_glInsertEventMarkerEXT         = (TKGL_InsertEventMarker) glLoader("glInsertEventMarkerEXT");
    tk_glPopGroupMarkerEXT            = (TKGL_PopGroupMarker) glLoader("glPopGroupMarkerEXT");
    tk_glPushGroupMarkerEXT           = (TKGL_PushGroupMarker) glLoader("glPushGroupMarkerEXT");
    tk_glLabelObjectEXT               = (TKGL_LabelObject) glLoader("glLabelObjectEXT");
    tk_glGetObjectLabelEXT            = (TKGL_GetObjectLabel) glLoader("glGetObjectLabelEXT");

    // String Checks for Extensions.
    PFNGLGETSTRINGPROC tk_glGetString = nullptr;
    tk_glGetString                    = (PFNGLGETSTRINGPROC) glLoader("glGetString");

    if (tk_glGetString)
    {
      const GLubyte* extensions = tk_glGetString(GL_EXTENSIONS);
      if (extensions != nullptr)
      {
        String extensionsStr((const char*) extensions);
        TK_GL_OES_texture_float_linear       = extensionsStr.find("GL_OES_texture_float_linear") != String::npos;
        TK_GL_EXT_texture_filter_anisotropic = extensionsStr.find("GL_EXT_texture_filter_anisotropic") != String::npos;
        TK_GL_EXT_depth_clamp                = extensionsStr.find("GL_EXT_depth_clamp") != String::npos;
      }
    }

#endif

#ifdef TK_WEB

    // Load WebGL extensions with emscripten for WebGL context
    tk_glRenderbufferStorageMultisampleEXT =
        (TKGL_RenderbufferStorageMultisample) emscripten_webgl_get_proc_address("glRenderbufferStorageMultisampleEXT");
    TK_LOG("glRenderbufferStorageMultisampleEXT address = %p", (void*) tk_glRenderbufferStorageMultisampleEXT);

    tk_glFramebufferTexture2DMultisampleEXT = (TKGL_FramebufferTexture2DMultisample) emscripten_webgl_get_proc_address(
        "glFramebufferTexture2DMultisampleEXT");
    TK_LOG("glFramebufferTexture2DMultisampleEXT address = %p", (void*) tk_glFramebufferTexture2DMultisampleEXT);

    tk_glInsertEventMarkerEXT = (TKGL_InsertEventMarker) emscripten_webgl_get_proc_address("glInsertEventMarkerEXT");
    tk_glPopGroupMarkerEXT    = (TKGL_PopGroupMarker) emscripten_webgl_get_proc_address("glPopGroupMarkerEXT");
    tk_glPushGroupMarkerEXT   = (TKGL_PushGroupMarker) emscripten_webgl_get_proc_address("glPushGroupMarkerEXT");
    tk_glLabelObjectEXT       = (TKGL_LabelObject) emscripten_webgl_get_proc_address("glLabelObjectEXT");
    tk_glGetObjectLabelEXT    = (TKGL_GetObjectLabel) emscripten_webgl_get_proc_address("glGetObjectLabelEXT");

    // Listing an extension in GL_EXTENSIONS is not the same as having it: WebGL turns an
    // extension's enums on only after the page asks for it. Ask for the ones the engine depends on
    // before the probes below can report them, or the probe answers "supported" and the call that
    // uses the extension is rejected:
    //   - EXT_texture_filter_anisotropic: TEXTURE_MAX_ANISOTROPY_EXT was rejected with INVALID_ENUM
    //     on every 2D texture that enabled anisotropic filtering.
    //   - OES_texture_float_linear: linear filtering of a float target (the shadow blur draws into
    //     an RG16F target and asks for SampleLinear) is rejected the same way.
    if (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_get_current_context())
    {
      emscripten_webgl_enable_extension(context, "EXT_texture_filter_anisotropic");
      emscripten_webgl_enable_extension(context, "OES_texture_float_linear");
    }

    // WebGL 2 is GL ES 3.0, where GL_EXTENSIONS is not a valid argument for
    // glGetString (it returns null there). Query the enumeration instead, and
    // accept the name with or without the "GL_" prefix: WebGL reports
    // extensions as "EXT_texture_filter_anisotropic", the GL headers use
    // "GL_EXT_texture_filter_anisotropic".
    auto HasExtension = [](const char* name) -> bool
    {
      GLint extensionCount = 0;
      glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
      for (GLint i = 0; i < extensionCount; ++i)
      {
        const char* extension = (const char*) glGetStringi(GL_EXTENSIONS, i);
        if (extension == nullptr)
        {
          continue;
        }
        const char* plainName = extension;
        if (std::strncmp(plainName, "GL_", 3) == 0)
        {
          plainName += 3;
        }
        if (std::strcmp(plainName, name) == 0)
        {
          return true;
        }
      }
      return false;
    };

    TK_GL_OES_texture_float_linear       = HasExtension("OES_texture_float_linear");
    TK_GL_EXT_texture_filter_anisotropic = HasExtension("EXT_texture_filter_anisotropic");
    TK_GL_EXT_depth_clamp                = HasExtension("EXT_depth_clamp");
#endif
  }

  bool HasObjectLabel()
  {
#ifdef TK_WEB
    return tk_glLabelObjectEXT != nullptr;
#else
    return glObjectLabel != nullptr;
#endif
  }

  void SetObjectLabel(GLenum identifier, GLuint object, GLsizei length, const GLchar* label)
  {
#ifdef TK_WEB
    if (tk_glLabelObjectEXT != nullptr)
    {
      tk_glLabelObjectEXT(identifier, object, length, label);
    }
#else
    if (glObjectLabel != nullptr)
    {
      glObjectLabel(identifier, object, length, label);
    }
#endif
  }

} // namespace ToolKit
