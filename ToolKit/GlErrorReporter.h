/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Types.h"

namespace ToolKit
{

  class TK_API GlErrorReporter
  {
   public:
    // Override it as you see fit. This lambda will be called with the opengl
    // error message.
    static GpuErrorCallback Report;
  };

  // Using standard types to avoid dependency on GL headers in this interface.
  // These are binary compatible with GLenum, GLuint, GLsizei, GLchar.
  TK_API void GLDebugMessageCallback(unsigned int source,
                                     unsigned int type,
                                     unsigned int id,
                                     unsigned int severity,
                                     int length,
                                     const char* msg,
                                     const void* data);

  TK_API void InitGLErrorReport(GpuErrorCallback callback = nullptr);

  TK_API unsigned int glCheckError_(const char* file, int line);

#ifdef TK_DEBUG
  #define TKCheckGL() glCheckError_(__FILE__, __LINE__)
#else
  #define TKCheckGL()
#endif

} // namespace ToolKit
