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
  typedef void(TK_STDCAL* TKGL_QueryCounterEXT)(GLuint id, GLenum target);
  typedef void(TK_STDCAL* TKGL_GetQueryObjectui64vEXT)(GLuint id, GLenum pname, GLuint64* params);

  extern TKGL_GenQueriesEXT tk_glGenQueriesEXT;
  extern TKGL_DeleteQueriesEXT tk_glDeleteQueriesEXT;
  extern TKGL_BeginQueryEXT tk_glBeginQueryEXT;
  extern TKGL_EndQueryEXT tk_glEndQueryEXT;
  extern TKGL_GetQueryObjectuivEXT tk_glGetQueryObjectuivEXT;
  extern TKGL_QueryCounterEXT tk_glQueryCounterEXT;
  extern TKGL_GetQueryObjectui64vEXT tk_glGetQueryObjectui64vEXT;

#ifndef GL_TIME_ELAPSED_EXT
  #define GL_TIME_ELAPSED_EXT 0x88BF
#endif

#ifndef GL_GPU_DISJOINT_EXT
  #define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

// A timestamp query writes the gpu clock into the query object instead of measuring an interval, so
// any number of them can be open at once. That is what per pass timing needs: GL ES allows a single
// GL_TIME_ELAPSED query at a time, and passes nest.
#ifndef GL_TIMESTAMP_EXT
  #define GL_TIMESTAMP_EXT 0x8E28
#endif

#ifndef GL_QUERY_RESULT_EXT
  #define GL_QUERY_RESULT_EXT 0x8866
#endif

#ifndef GL_QUERY_RESULT_AVAILABLE_EXT
  #define GL_QUERY_RESULT_AVAILABLE_EXT 0x8867
#endif

  // Block compressed texture formats
  //////////////////////////////////////////
  //
  // None of the S3TC / BPTC / ASTC enums come from the headers this file builds against: glad's
  // generated header carries the core GL 4.x set but not the EXT/KHR spellings, and emscripten's
  // GLES3 headers stop at the ETC2 enums that ES 3.0 made core. The values are the registry ones.

  #ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
    #define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
  #endif

  #ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
  #endif

  #ifndef GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
    #define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C
  #endif

  #ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
    #define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
  #endif

  #ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
    #define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
  #endif

  #ifndef GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM
    #define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM 0x8E8D
  #endif

  #ifndef GL_COMPRESSED_RGB8_ETC2
    #define GL_COMPRESSED_RGB8_ETC2 0x9274
  #endif

  #ifndef GL_COMPRESSED_SRGB8_ETC2
    #define GL_COMPRESSED_SRGB8_ETC2 0x9275
  #endif

  #ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
    #define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
  #endif

  #ifndef GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC
    #define GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC 0x9279
  #endif

  #ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
    #define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
  #endif

  #ifndef GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR
    #define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR 0x93D0
  #endif

  // GL Loader function
  //////////////////////////////////////////

  extern void LoadGlFunctions(void* glGetProcAddres);

  /**
   * True when the driver lists the extension. The name is matched with or without its "GL_" prefix,
   * and the browser's "WEBGL_*" spelling is matched by passing it verbatim.
   *
   * GL ES 3.0 removed the GL_EXTENSIONS string -- glGetString answers null for it -- so the list is
   * enumerated through GL_NUM_EXTENSIONS / glGetStringi on every platform.
   */
  TK_API bool HasGlExtension(const char* name);

  /**
   * Makes a WebGL extension's enums usable. A no-op everywhere else.
   *
   * On the web an extension that merely appears in GL_EXTENSIONS is not enabled yet, and using its
   * enums raises INVALID_ENUM. Listing is not having (see LoadGlFunctions).
   */
  TK_API void EnableGlExtension(const char* name);

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
