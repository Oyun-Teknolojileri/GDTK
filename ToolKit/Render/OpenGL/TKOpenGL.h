/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Platform.h"

#ifdef TK_ANDROID
  #include <GLES3/gl32.h>
#elif defined(TK_WEB)
  #include <GLES3/gl32.h>
#else
  #include <glad/gl.h>
#endif

#include "GlErrorReporter.h"

namespace ToolKit
{

  // GL Extensions used by ToolKit.

  // GL_EXT_debug_label
  //////////////////////////////////////////
  typedef void(
      TK_STDCAL* TKGL_GetObjectLabel)(GLenum type, GLuint object, GLsizei bufSize, GLsizei* length, GLchar* label);

  extern TKGL_GetObjectLabel tk_glGetObjectLabelEXT;

#undef glGetObjectLabelEXT
#define glGetObjectLabelEXT tk_glGetObjectLabelEXT

  typedef void(TK_STDCAL* TKGL_LabelObject)(GLenum type, GLuint object, GLsizei length, const GLchar* label);

  extern TKGL_LabelObject tk_glLabelObjectEXT;

#undef glLabelObjectEXT
#define glLabelObjectEXT tk_glLabelObjectEXT

  // GL_EXT_debug_marker
  //////////////////////////////////////////

  typedef void(TK_STDCAL* TKGL_InsertEventMarker)(GLsizei length, const GLchar* marker);

  extern TKGL_InsertEventMarker tk_glInsertEventMarkerEXT;

#undef glInsertEventMarkerEXT
#define glInsertEventMarkerEXT tk_glInsertEventMarkerEXT

  typedef void(TK_STDCAL* TKGL_PopGroupMarker)(void);

  extern TKGL_PopGroupMarker tk_glPopGroupMarkerEXT;

#undef glPopGroupMarkerEXT
#define glPopGroupMarkerEXT tk_glPopGroupMarkerEXT

  typedef void(TK_STDCAL* TKGL_PushGroupMarker)(GLsizei length, const GLchar* marker);

  extern TKGL_PushGroupMarker tk_glPushGroupMarkerEXT;

#undef glPushGroupMarkerEXT
#define glPushGroupMarkerEXT tk_glPushGroupMarkerEXT

  // GL_EXT_multisampled_render_to_texture
  //////////////////////////////////////////

  typedef void(TK_STDCAL* TKGL_FramebufferTexture2DMultisample)(GLenum target,
                                                                GLenum attachment,
                                                                GLenum textarget,
                                                                GLuint texture,
                                                                GLint level,
                                                                GLsizei samples);

  extern TKGL_FramebufferTexture2DMultisample tk_glFramebufferTexture2DMultisampleEXT;

#undef glFramebufferTexture2DMultisampleEXT
#define glFramebufferTexture2DMultisampleEXT tk_glFramebufferTexture2DMultisampleEXT

  typedef void(TK_STDCAL* TKGL_RenderbufferStorageMultisample)(GLenum target,
                                                               GLsizei samples,
                                                               GLenum internalformat,
                                                               GLsizei width,
                                                               GLsizei height);

  extern TKGL_RenderbufferStorageMultisample tk_glRenderbufferStorageMultisampleEXT;

#undef glRenderbufferStorageMultisampleEXT
#define glRenderbufferStorageMultisampleEXT tk_glRenderbufferStorageMultisampleEXT

  // GL_EXT_texture_filter_anisotropic
  //////////////////////////////////////////

  extern int TK_GL_EXT_texture_filter_anisotropic;

  /**
   * Resolves an entry point through the GL loader the host provided. Android needs it for the
   * program binary extension (GL_OES_get_program_binary), which glad's generated header does not
   * declare.
   */
  TK_API void* TKGetGlProcAddress(const char* name);

#undef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

#undef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE

  // GL_OES_texture_float_linear
  //////////////////////////////////////////

  extern int TK_GL_OES_texture_float_linear;

  // GL_DEPTH_CLAMP_EXT
  //////////////////////////////////////////

  extern int TK_GL_EXT_depth_clamp;

#undef GL_DEPTH_CLAMP_EXT
#define GL_DEPTH_CLAMP_EXT 0x864F

  // GL_EXT_Invalidate_subdata
  //////////////////////////////////////////

  extern int TK_GL_EXT_Invalidate_subdata;

  typedef void(TK_STDCAL* TKGL_InvalidateFramebuffer)(GLenum target, GLsizei numAttachments, const GLenum* attachments);

  extern TKGL_InvalidateFramebuffer tk_glInvalidateFramebufferEXT;

#undef glInvalidateFramebufferEXT
#define glInvalidateFramebufferEXT tk_glInvalidateFramebufferEXT

  // GL_EXT_disjoint_timer_query
  //////////////////////////////////////////
  //
  // GL ES has no core timer query: GL_TIME_ELAPSED exists only as part of this extension, and the
  // entry points that take it carry the EXT suffix. Desktop GL reaches the same enum through glad's
  // core 3.3 (ARB_timer_query) declarations, which is why the backend keeps two call paths for one
  // measurement. A query answers in nanoseconds; Stats::GetRenderTime reports it as a frame's gpu
  // time.

  extern int TK_GL_EXT_disjoint_timer_query;

  typedef void(TK_STDCAL* TKGL_GenQueriesEXT)(GLsizei n, GLuint* ids);
  typedef void(TK_STDCAL* TKGL_DeleteQueriesEXT)(GLsizei n, const GLuint* ids);
  typedef void(TK_STDCAL* TKGL_BeginQueryEXT)(GLenum target, GLuint id);
  typedef void(TK_STDCAL* TKGL_EndQueryEXT)(GLenum target);
  typedef void(TK_STDCAL* TKGL_GetQueryObjectuivEXT)(GLuint id, GLenum pname, GLuint* params);

  extern TKGL_GenQueriesEXT tk_glGenQueriesEXT;
  extern TKGL_DeleteQueriesEXT tk_glDeleteQueriesEXT;
  extern TKGL_BeginQueryEXT tk_glBeginQueryEXT;
  extern TKGL_EndQueryEXT tk_glEndQueryEXT;
  extern TKGL_GetQueryObjectuivEXT tk_glGetQueryObjectuivEXT;

#ifndef GL_TIME_ELAPSED_EXT
  #define GL_TIME_ELAPSED_EXT 0x88BF
#endif

#ifndef GL_GPU_DISJOINT_EXT
  #define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

  // GL Loader function
  //////////////////////////////////////////

  extern void LoadGlFunctions(void* glGetProcAddres);

  // Object debug labels
  //////////////////////////////////////////
  //
  // glObjectLabel() is desktop GL 4.3 / GL ES 3.2. WebGL 2 is GL ES 3.0, where
  // the entry point does not exist at all -- referencing it is a link error, not
  // a null pointer. These two wrappers keep the call sites free of the platform
  // split: on the web the label goes through the GL_EXT_debug_label function
  // pointer the engine already resolves in LoadGlFunctions().

  /** True when the driver exposes an entry point that can label GL objects. */
  TK_API bool HasObjectLabel();

  /** Labels a GL object with the entry point the active driver provides. */
  TK_API void SetObjectLabel(GLenum identifier, GLuint object, GLsizei length, const GLchar* label);

} // namespace ToolKit
