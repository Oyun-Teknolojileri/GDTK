/*
 /*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "GLBackend.h"

#include "EngineSettings.h"
#include "Framebuffer.h"
#include "GlErrorReporter.h"
#include "GpuProgram.h"
#include "Renderer.h"
#include "Mesh.h"
#include "UniformBuffer.h"
#include "Mesh.h"
#include "PerDrawUniforms.h"
#include "RHI.h"
#include "ShaderUniform.h"
#include "TKOpenGL.h"
#include "Texture.h"
#include "ToolKit.h"
#include "Util.h"
#include "DebugNew.h"

namespace ToolKit
{

  // -----------------------------------------------------------------------
  // GL enum mapping tables
  // -----------------------------------------------------------------------

  static GLenum ToGLGraphicType(GraphicTypes t)
  {
    // clang-format off
    static constexpr GLenum table[] =
    {
      GL_VERTEX_SHADER,           // VertexShader
      GL_FRAGMENT_SHADER,         // FragmentShader
      GL_REPEAT,                  // UVRepeat
      GL_CLAMP_TO_EDGE,           // UVClampToEdge
      0x812D,                     // UVClampToBorder (GL_CLAMP_TO_BORDER)
      GL_NEAREST,                 // SampleNearest
      GL_LINEAR,                  // SampleLinear
      GL_NEAREST_MIPMAP_NEAREST,  // SampleNearestMipmapNearest
      GL_LINEAR_MIPMAP_LINEAR,    // SampleLinearMipmapLinear
      GL_LINEAR_MIPMAP_NEAREST,   // SampleLinearMipmapNearest
      GL_RED,                     // FormatRed
      GL_R8,                      // FormatR8
      GL_RG,                      // FormatRG
      GL_RG8,                     // FormatRG8
      GL_RGB,                     // FormatRGB
      GL_RGB8,                    // FormatRGB8
      GL_RGBA8,                   // FormatRGBA8
      GL_RGBA,                    // FormatRGBA
      GL_R16F,                    // FormatR16F
      GL_R32F,                    // FormatR32F
      GL_RG16F,                   // FormatRG16F
      GL_RG32F,                   // FormatRG32F
      GL_RGB16F,                  // FormatRGB16F
      GL_RGBA16F,                 // FormatRGBA16F
      GL_RGB32F,                  // FormatRGB32F
      GL_RGBA32F,                 // FormatRGBA32F
      0x8F98,                     // FormatR16SNorm (GL_R16_SNORM)
      GL_SRGB8_ALPHA8,            // FormatSRGB8_A8
      GL_DEPTH_COMPONENT24,       // FormatDepth24
      GL_DEPTH24_STENCIL8,        // FormatDepth24Stencil8
      GL_COLOR_ATTACHMENT0,       // ColorAttachment0
      GL_DEPTH_ATTACHMENT,        // DepthAttachment
      GL_FLOAT,                   // TypeFloat
      GL_UNSIGNED_BYTE,           // TypeUnsignedByte
      GL_TEXTURE_2D,              // Target2D
      GL_TEXTURE_CUBE_MAP,        // TargetCubeMap
      GL_TEXTURE_2D_ARRAY         // Target2DArray
    };
    // clang-format on
    return table[(int) t];
  }

  static GLbitfield ToGLBitfield(GraphicBitFields f)
  {
    GLbitfield bits = 0;
    if ((int) f & (int) GraphicBitFields::ColorBits)   bits |= GL_COLOR_BUFFER_BIT;
    if ((int) f & (int) GraphicBitFields::DepthBits)    bits |= GL_DEPTH_BUFFER_BIT;
    if ((int) f & (int) GraphicBitFields::StencilBits)  bits |= GL_STENCIL_BUFFER_BIT;
    return bits;
  }

  static GLenum ToGLCompareFunc(CompareFunctions f)
  {
    static constexpr GLenum table[] =
    {
      GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS
    };
    return table[(int) f];
  }

  static GLenum ToGLDrawType(DrawType t)
  {
    static constexpr GLenum table[] = { GL_POINTS, GL_LINES, GL_LINE_LOOP, GL_LINE_STRIP, GL_TRIANGLES };
    return table[(int) t];
  }

  // GL-specific GPU resource data structs.
  // These are allocated by GLBackend and stored in resource m_gpuData.

  struct GLTextureData : GpuResourceData
  {
    GLuint textureId     = 0;
    bool isRenderbuffer  = false;
  };

  struct GLFramebufferData : GpuResourceData
  {
    GLuint fboId = 0;
  };

  struct GLMeshData : GpuResourceData
  {
    GLuint vaoId       = 0;
    GLuint vboVertexId = 0;
    GLuint vboIndexId  = 0;
  };

  struct GLUniformBufferData : GpuResourceData
  {
    GLuint uboId = 0;
  };

  struct GLProgramData : GpuResourceData
  {
    GLuint programId = 0;
  };

  struct GLShaderData : GpuResourceData
  {
    GLuint shaderId = 0;
  };

  // Helper accessors — safe static_cast from base.
  static GLTextureData* GetGLTextureData(Texture* tex)
  {
    return static_cast<GLTextureData*>(tex->m_gpuData.get());
  }

  static GLFramebufferData* GetGLFramebufferData(Framebuffer* fb)
  {
    return static_cast<GLFramebufferData*>(fb->m_gpuData.get());
  }

  static GLMeshData* GetGLMeshData(const Mesh* mesh)
  {
    return static_cast<GLMeshData*>(mesh->m_gpuData.get());
  }

  static GLProgramData* GetGLProgramData(GpuProgram* program)
  {
    return static_cast<GLProgramData*>(program->m_gpuData.get());
  }

  static GLuint GetGLProgramId(GpuProgram* program)
  {
    GLProgramData* gl = GetGLProgramData(program);
    return gl ? gl->programId : 0;
  }

  GLBackend::GLBackend()  {}

  GLBackend::~GLBackend()
  {
    if (m_gpuTimerQuery)
    {
      glDeleteQueries(1, &m_gpuTimerQuery);
      m_gpuTimerQuery = 0;
    }
  }

  void GLBackend::InitBackend(const BackendInitParams& params)
  {
    LoadGlFunctions(params.getProcAddress);
    InitGLErrorReport(params.errorCallback);

    // Default states.
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

#ifdef GL_TEXTURE_CUBE_MAP_SEAMLESS
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
#endif
  }

  void GLBackend::BeginFrame()
  {
    m_firstBind = true;
  }

  void GLBackend::EndFrame() {}

  void GLBackend::Present() {}

  void GLBackend::BeginPass(const PassDesc& desc)
  {
    m_activePassDesc = desc;

    if (desc.target != nullptr)
    {
      BindFramebuffer(GL_FRAMEBUFFER, GetGLFramebufferData(desc.target.get())->fboId);
      SetDrawBuffers(desc.target.get());
    }
    else
    {
      BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    if (desc.clearBits != GraphicBitFields::None)
    {
      ClearBuffer(desc.clearBits, desc.clearColor);
    }
  }

  void GLBackend::EndPass()
  {
    GraphicBitFields bits = m_activePassDesc.discardBits;

    if (m_activePassDesc.target == nullptr || bits == GraphicBitFields::None)
    {
      return;
    }

    FramebufferPtr fb = m_activePassDesc.target;

    BindFramebuffer(GL_DRAW_FRAMEBUFFER, GetGLFramebufferData(fb.get())->fboId);

    GLenum attachments[3];
    int count = 0;

    if (fb->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0))
    {
      if ((int) bits & (int) GraphicBitFields::ColorBits)
      {
        attachments[count++] = GL_COLOR_ATTACHMENT0;
      }
    }

    if (DepthTexturePtr dt = fb->GetDepthTexture())
    {
      if ((int) bits & (int) GraphicBitFields::DepthBits)
      {
        bool hasStencil      = ((int) bits & (int) GraphicBitFields::StencilBits) && dt->m_stencil;
        attachments[count++] = hasStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
      }
      else if (((int) bits & (int) GraphicBitFields::StencilBits) && dt->m_stencil)
      {
        attachments[count++] = GL_STENCIL_ATTACHMENT;
      }
    }

    if (count == 0)
    {
      return;
    }

#ifdef TK_GL_ES_3_0
    glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, count, attachments);
#else
    if (glInvalidateFramebufferEXT != nullptr)
    {
      glInvalidateFramebufferEXT(GL_DRAW_FRAMEBUFFER, count, attachments);
    }
#endif

    m_activePassDesc.discardBits = GraphicBitFields::None;
  }

  void GLBackend::StoreFboBindings()
  {
    m_storedReadFboStack.push_back(m_currentReadFboId);
    m_storedDrawFboStack.push_back(m_currentDrawFboId);
  }

  void GLBackend::RestoreFboBindings()
  {
    if (!m_storedReadFboStack.empty())
    {
      BindFramebuffer(GL_READ_FRAMEBUFFER, m_storedReadFboStack.back());
      m_storedReadFboStack.pop_back();
    }

    if (!m_storedDrawFboStack.empty())
    {
      BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_storedDrawFboStack.back());
      m_storedDrawFboStack.pop_back();
    }
  }

  void GLBackend::SetViewport(uint x, uint y, uint w, uint h)
  {
    glViewport(x, y, w, h);
  }

  void GLBackend::SetScissor(uint x, uint y, uint w, uint h)
  {
    glScissor(x, y, w, h);
  }

  void GLBackend::ClearBuffer(GraphicBitFields fields, const Vec4& color)
  {
    // OpenGL clears are affected by masks and scissor test. 
    // Force them to safe defaults to ensure the clear operation 
    // succeeds regardless of current pipeline state.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    glDisable(GL_SCISSOR_TEST);

    glClearColor(color.x, color.y, color.z, color.w);
    glClear(ToGLBitfield(fields));

    // Invalidate state cache to force re-application of masks and scissor on next draw.
    m_firstBind = true;
  }

  void GLBackend::ClearColorBuffer(const Vec4& color)
  {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(color.x, color.y, color.z, color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    m_firstBind = true;
  }

  void GLBackend::BindPipeline(const GpuProgramPtr& program, const RenderState* state)
  {
    if (program)
    {
      glUseProgram(GetGLProgramId(program.get()));
    }

    if (state == nullptr)
    {
      return;
    }

    // Culling
    if (m_firstBind || m_lastAppliedState.cullMode != state->cullMode)
    {
      if (state->cullMode == CullingType::TwoSided)
      {
        glDisable(GL_CULL_FACE);
      }
      else
      {
        glEnable(GL_CULL_FACE);
        glCullFace(state->cullMode == CullingType::Front ? GL_FRONT : GL_BACK);
      }
      m_lastAppliedState.cullMode = state->cullMode;
    }

    // Depth Test
    if (m_firstBind || m_lastAppliedState.depthTestEnabled != state->depthTestEnabled)
    {
      if (state->depthTestEnabled)
      {
        glEnable(GL_DEPTH_TEST);
      }
      else
      {
        glDisable(GL_DEPTH_TEST);
      }
      m_lastAppliedState.depthTestEnabled = state->depthTestEnabled;
    }

    if (state->depthTestEnabled && (m_firstBind || m_lastAppliedState.depthFunction != state->depthFunction))
    {
      glDepthFunc(ToGLCompareFunc(state->depthFunction));
      m_lastAppliedState.depthFunction = state->depthFunction;
    }

    // Depth Write
    if (m_firstBind || m_lastAppliedState.depthWriteEnabled != state->depthWriteEnabled)
    {
      glDepthMask(state->depthWriteEnabled ? GL_TRUE : GL_FALSE);
      m_lastAppliedState.depthWriteEnabled = state->depthWriteEnabled;
    }

    // Depth Clamp
    if (m_firstBind || m_lastAppliedState.depthClampEnabled != state->depthClampEnabled)
    {
      if (TK_GL_EXT_depth_clamp)
      {
        if (state->depthClampEnabled)
        {
          glEnable(GL_DEPTH_CLAMP_EXT);
        }
        else
        {
          glDisable(GL_DEPTH_CLAMP_EXT);
        }
      }
      m_lastAppliedState.depthClampEnabled = state->depthClampEnabled;
    }

    // Blending
    if (m_firstBind || m_lastAppliedState.blendFunction != state->blendFunction)
    {
      if (state->blendFunction == BlendFunction::NONE)
      {
        glDisable(GL_BLEND);
      }
      else
      {
        glEnable(GL_BLEND);
        switch (state->blendFunction)
        {
          case BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
          case BlendFunction::ONE_TO_ONE:
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_ADD);
            break;
          default:
            glDisable(GL_BLEND);
            break;
        }
      }
      m_lastAppliedState.blendFunction = state->blendFunction;
    }

    // Stencil
    if (m_firstBind || m_lastAppliedState.stencilOperation != state->stencilOperation)
    {
      switch (state->stencilOperation)
      {
        case StencilOperation::None:
          glDisable(GL_STENCIL_TEST);
          glStencilMask(0x00);
          break;
        case StencilOperation::AllowAllPixels:
          glEnable(GL_STENCIL_TEST);
          glStencilMask(0xFF);
          glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
          glStencilFunc(GL_ALWAYS, 0xFF, 0xFF);
          break;
        case StencilOperation::AllowPixelsPassingStencil:
          glEnable(GL_STENCIL_TEST);
          glStencilFunc(GL_EQUAL, 0xFF, 0xFF);
          glStencilMask(0x00);
          break;
        case StencilOperation::AllowPixelsFailingStencil:
          glEnable(GL_STENCIL_TEST);
          glStencilFunc(GL_NOTEQUAL, 0xFF, 0xFF);
          glStencilMask(0x00);
          break;
      }
      m_lastAppliedState.stencilOperation = state->stencilOperation;
    }

    // Color Mask
    if (m_firstBind || m_lastAppliedState.colorMaskEnabled != state->colorMaskEnabled)
    {
      GLboolean m = state->colorMaskEnabled ? GL_TRUE : GL_FALSE;
      glColorMask(m, m, m, m);
      m_lastAppliedState.colorMaskEnabled = state->colorMaskEnabled;
    }

    m_firstBind = false;
  }

  void GLBackend::SubmitPerDrawData(const void* data, size_t size)
  {
    if (data == nullptr || size != sizeof(PerDrawUniforms))
    {
      return;
    }

    const PerDrawUniforms* pdu = reinterpret_cast<const PerDrawUniforms*>(data);
    GLuint currentProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*) &currentProgram);
    if (currentProgram == 0)
    {
      return;
    }

    // Built-in uniforms.
    auto setMat4 = [&](Uniform u, const Mat4& m)
    {
      GLint loc = glGetUniformLocation(currentProgram, GetUniformName(u));
      if (loc != -1)
      {
        glUniformMatrix4fv(loc, 1, false, reinterpret_cast<const float*>(&m));
      }
    };

    setMat4(Uniform::MODEL, pdu->model);
    setMat4(Uniform::MODEL_WITHOUT_TRANSLATE, pdu->modelWithoutTranslate);
    setMat4(Uniform::INVERSE_MODEL, pdu->inverseModel);
    setMat4(Uniform::INVERSE_TRANSPOSE_MODEL, pdu->inverseTransposeModel);
    setMat4(Uniform::IBL_ROTATION, pdu->iblRotation);
    setMat4(Uniform::IBL_SECONDARY_ROTATION, pdu->iblSecondaryRotation);

    GLint vpLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::VIEWPORT_SIZE));
    if (vpLoc != -1)
    {
      glUniform2f(vpLoc, pdu->viewportSize.x, pdu->viewportSize.y);
    }

    // DrawCommand.
    GLint dcLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::DRAW_COMMAND));
    if (dcLoc != -1)
    {
      glUniform4fv(dcLoc, sizeof(DrawCommand) / sizeof(Vec4), (const float*) &pdu->drawCommand);
    }

    // Light indices.
    GLint plLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::ACTIVE_POINT_LIGHT_INDEXES));
    if (plLoc != -1 && pdu->activePointLightCount > 0)
    {
      glUniform1iv(plLoc, pdu->activePointLightCount, pdu->activePointLightIndices);
    }

    GLint slLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::ACTIVE_SPOT_LIGHT_INDEXES));
    if (slLoc != -1 && pdu->activeSpotLightCount > 0)
    {
      glUniform1iv(slLoc, pdu->activeSpotLightCount, pdu->activeSpotLightIndices);
    }

    // Material data.
    GLint matLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::MATERIAL_CACHE));
    if (matLoc != -1)
    {
      glUniform4fv(matLoc, sizeof(MaterialCacheItem::Data) / sizeof(Vec4), (const float*) &pdu->materialData);
    }

    // Animation data.
    GLint kfLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::KEY_FRAME_DATA));
    if (kfLoc != -1)
    {
      glUniform4fv(kfLoc, 1, (const float*) &pdu->keyFrameData);
    }

    GLint bkfLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::BLEND_FRAME_DATA));
    if (bkfLoc != -1)
    {
      glUniform4fv(bkfLoc, 1, (const float*) &pdu->blendFrameData);
    }

    GLint bfLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::BLEND_FACTOR));
    if (bfLoc != -1)
    {
      glUniform1f(bfLoc, pdu->animationBlendFactor);
    }

    GLint spLoc = glGetUniformLocation(currentProgram, GetUniformName(Uniform::SKIN_PARAMS));
    if (spLoc != -1)
    {
      glUniform4fv(spLoc, 1, (const float*) &pdu->skinParams);
    }
  }

  void GLBackend::BindTexture(ubyte slot, TexturePtr tex)
  {
    if (tex != nullptr)
    {
      GLTextureData* gl = GetGLTextureData(tex.get());
      if (gl) BindTextureDirect(ToGLGraphicType(tex->Settings().Target), gl->textureId, slot);
    }
    else
    {
      BindTextureDirect(GL_TEXTURE_2D, 0, slot);
    }
  }

  void GLBackend::Draw(const DrawDesc& desc)
  {
    if (desc.mesh == nullptr)
    {
      return;
    }

    GLMeshData* meshGl = GetGLMeshData(desc.mesh);
    assert(meshGl != nullptr && "Mesh has no GPU data. Was CreateMesh called?");
    if (meshGl == nullptr || meshGl->vaoId == 0)
    {
      return;
    }

    BindVAO(meshGl->vaoId);

    if (desc.indexed)
    {
      glDrawElements(ToGLDrawType(desc.type), desc.elementCount, GL_UNSIGNED_INT, nullptr);
    }
    else
    {
      glDrawArrays(ToGLDrawType(desc.type), 0, desc.elementCount);
    }
  }

  // -----------------------------------------------------------------------
  // Mesh resource management
  // -----------------------------------------------------------------------

  void GLBackend::CreateMesh(Mesh* mesh)
  {
    // Destroy existing GPU resources (re-upload path).
    DestroyMesh(mesh);

    auto glData = std::make_shared<GLMeshData>();

    const void* vertexData   = mesh->GetClientVertexData();
    size_t vertexCount       = mesh->GetClientVertexCount();

    if (vertexData != nullptr && vertexCount > 0)
    {
      // --- Vertex buffer ---
      glGenVertexArrays(1, &glData->vaoId);
      BindVAO(glData->vaoId);

      glGenBuffers(1, &glData->vboVertexId);
      glBindBuffer(GL_ARRAY_BUFFER, glData->vboVertexId);
      glBufferData(GL_ARRAY_BUFFER,
                   mesh->GetVertexSize() * vertexCount,
                   vertexData,
                   GL_STATIC_DRAW);

      // --- Vertex attribute layout (baked into VAO) ---
      if (mesh->m_vertexLayout == VertexLayout::SkinMesh)
      {
        GLuint offset = 0;
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), (void*) (uintptr_t) offset);
        offset += 3 * sizeof(float);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), (void*) (uintptr_t) offset);
        offset += 3 * sizeof(float);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), (void*) (uintptr_t) offset);
        offset += 2 * sizeof(float);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), (void*) (uintptr_t) offset);
        offset += 4 * sizeof(float);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), (void*) (uintptr_t) offset);
        offset += 4 * sizeof(float);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(SkinVertex), (void*) (uintptr_t) offset);
      }
      else // VertexLayout::Mesh (default)
      {
        GLuint offset = 0;
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*) (uintptr_t) offset);
        offset += 3 * sizeof(float);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*) (uintptr_t) offset);
        offset += 3 * sizeof(float);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*) (uintptr_t) offset);
        offset += 2 * sizeof(float);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*) (uintptr_t) offset);
      }

      // --- Index buffer ---
      if (!mesh->m_clientSideIndices.empty())
      {
        glGenBuffers(1, &glData->vboIndexId);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glData->vboIndexId);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     sizeof(uint) * mesh->m_clientSideIndices.size(),
                     mesh->m_clientSideIndices.data(),
                     GL_STATIC_DRAW);
        mesh->m_indexCount = (uint) mesh->m_clientSideIndices.size();
      }

      mesh->m_vertexCount = (uint) vertexCount;
    }

    mesh->m_gpuData = std::move(glData);
  }

  void GLBackend::DestroyMesh(Mesh* mesh)
  {
    GLMeshData* gl = mesh ? GetGLMeshData(mesh) : nullptr;
    if (gl == nullptr)
    {
      return;
    }

    if (gl->vboVertexId || gl->vboIndexId)
    {
      GLuint buffers[2] = {gl->vboIndexId, gl->vboVertexId};
      glDeleteBuffers(2, buffers);
    }

    if (gl->vaoId)
    {
      glDeleteVertexArrays(1, &gl->vaoId);
      BindVAO(0);
    }

    mesh->m_gpuData.reset();
  }

  // -----------------------------------------------------------------------
  // UniformBuffer resource management
  // -----------------------------------------------------------------------

  void GLBackend::CreateUniformBuffer(UniformBuffer* ub, uint64 size)
  {
    ub->m_size = size;
    auto glData = std::make_shared<GLUniformBufferData>();
    glGenBuffers(1, &glData->uboId);
    glBindBuffer(GL_UNIFORM_BUFFER, glData->uboId);
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr) size, nullptr, GL_DYNAMIC_DRAW);
    ub->m_gpuData = std::move(glData);
  }

  void GLBackend::DestroyUniformBuffer(UniformBuffer* ub)
  {
    if (auto* gl = static_cast<GLUniformBufferData*>(ub->m_gpuData.get()))
    {
      glDeleteBuffers(1, &gl->uboId);
    }
    ub->m_gpuData.reset();
  }

  void GLBackend::UpdateUniformBuffer(UniformBuffer* ub, const void* data, uint64 size)
  {
    auto* gl = static_cast<GLUniformBufferData*>(ub->m_gpuData.get());
    glBindBuffer(GL_UNIFORM_BUFFER, gl->uboId);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr) size, data);
  }

  // -----------------------------------------------------------------------
  // Shader resource management
  // -----------------------------------------------------------------------

  GpuResourceDataPtr GLBackend::CreateShader(Shader* shader, const String& source)
  {
    GLenum type = 0;
    if (shader->m_shaderType == ShaderType::VertexShader)
    {
      type = ToGLGraphicType(GraphicTypes::VertexShader);
    }
    else if (shader->m_shaderType == ShaderType::FragmentShader)
    {
      type = ToGLGraphicType(GraphicTypes::FragmentShader);
    }
    else
    {
      TK_ERR("Include shader can't be compiled: %s", shader->GetFile().c_str());
      return nullptr;
    }

    GLuint handle = glCreateShader(type);
    if (handle == 0)
    {
      return nullptr;
    }

    String src         = source;
    const char* str    = nullptr;
    size_t loc         = src.find("#version");
    if (loc != String::npos)
    {
      src = src.substr(loc);
    }
    str = src.c_str();

    glShaderSource(handle, 1, &str, nullptr);
    glCompileShader(handle);

    GLint compiled;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
      GLint infoLen = 0;
      glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &infoLen);
      if (infoLen > 1)
      {
        char* log = new char[infoLen];
        glGetShaderInfoLog(handle, infoLen, nullptr, log);
        TK_ERR(log);
        SafeDelArray(log);
      }
      glDeleteShader(handle);
      return nullptr;
    }

    auto data      = std::make_shared<GLShaderData>();
    data->shaderId = handle;
    return data;
  }

  void GLBackend::DestroyShader(GpuResourceData* shaderData)
  {
    if (auto* gl = static_cast<GLShaderData*>(shaderData))
    {
      if (gl->shaderId)
      {
        glDeleteShader(gl->shaderId);
        gl->shaderId = 0;
      }
    }
  }

  // -----------------------------------------------------------------------
  // GpuProgram resource management
  // -----------------------------------------------------------------------

  void GLBackend::CreateGpuProgram(GpuProgram* program, GlobalGpuBuffers* buffers)
  {
    const ShaderPtr& vs = program->m_shaders[0];
    const ShaderPtr& fs = program->m_shaders[1];

    auto glData = std::make_shared<GLProgramData>();
    glData->programId = glCreateProgram();
    GLuint pid        = glData->programId;

    glAttachShader(pid, static_cast<GLShaderData*>(vs->m_gpuData.get())->shaderId);
    glAttachShader(pid, static_cast<GLShaderData*>(fs->m_gpuData.get())->shaderId);
    glLinkProgram(pid);

    GLint linked = 0;
    glGetProgramiv(pid, GL_LINK_STATUS, &linked);
    if (!linked)
    {
      GLint infoLen = 0;
      glGetProgramiv(pid, GL_INFO_LOG_LENGTH, &infoLen);
      if (infoLen > 1)
      {
        char* log = new char[infoLen];
        glGetProgramInfoLog(pid, infoLen, nullptr, log);
        TK_ERR("Linking failed.\nVertex: %s\nFragment: %s\n%s",
               vs->GetFile().c_str(),
               fs->GetFile().c_str(),
               log);
        SafeDelArray(log);
      }
      glDeleteProgram(pid);
      return;
    }

    program->m_gpuData = std::move(glData);

    // Save and restore the currently bound program.
    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    glUseProgram(pid);

    // Bind sampler slots so shader samplers map to texture bind slots.
    for (ubyte slot = 0; slot < RHIConstants::TextureSlotCount; slot++)
    {
      GLint loc = glGetUniformLocation(pid, ("s_texture" + std::to_string(slot)).c_str());
      if (loc != -1)
      {
        glUniform1i(loc, slot);
      }
    }

    // Bind all known UBO blocks.
    auto bindUBO = [&](const char* blockName, int bindingSlot, UniformBuffer* ub)
    {
      int loc = glGetUniformBlockIndex(pid, blockName);
      if (loc != GL_INVALID_INDEX)
      {
        GLuint uboId = static_cast<GLUniformBufferData*>(ub->m_gpuData.get())->uboId;
        glUniformBlockBinding(pid, loc, bindingSlot);
        glBindBufferBase(GL_UNIFORM_BUFFER, bindingSlot, uboId);
      }
    };

    bindUBO("CameraData",              CameraGpuBuffer::Binding(),          &buffers->cameraGpuBuffer.GetBuffer());
    bindUBO("GraphicConstatsData",     GraphicConstantsGpuBuffer::Binding(), &buffers->graphicConstantBuffer.GetBuffer());
    bindUBO("DirectionalLightBuffer",  DirectionalLightBuffer::BindingSlotForLight, &buffers->directionalLightBuffer.m_lightDataBuffer);
    bindUBO("DirectionalLightPVMBuffer", DirectionalLightBuffer::BindingSlotForPVM, &buffers->directionalLightBuffer.m_pvms);
    bindUBO("PointLightCache",         PointLightCache::BindingSlot,        &buffers->pointLighBuffer.m_gpuBuffer);
    bindUBO("SpotLightCache",          SpotLightCache::BindingSlot,         &buffers->spotLightBuffer.m_gpuBuffer);

    // Cache default and array uniform locations.
    for (const ShaderPtr& shader : program->m_shaders)
    {
      for (const Uniform& uniform : shader->m_uniforms)
      {
        GLint loc                                  = glGetUniformLocation(pid, GetUniformName(uniform));
        program->m_defaultUniformLocation[uniform] = loc;
      }

      for (Shader::ArrayUniform arrayUniform : shader->m_arrayUniforms)
      {
        GLint loc = glGetUniformLocation(pid, GetUniformName(arrayUniform.uniform));
        program->m_defaultArrayUniformLocations[arrayUniform.uniform] = loc;
      }
    }

    glUseProgram(currentProgram);
  }

  void GLBackend::DestroyGpuProgram(GpuProgram* program)
  {
    if (GLProgramData* gl = GetGLProgramData(program))
    {
      glDeleteProgram(gl->programId);
    }
    program->m_gpuData.reset();
  }

  int GLBackend::GetUniformLocation(GpuProgram* program, const char* name)
  {
    return glGetUniformLocation(GetGLProgramId(program), name);
  }

  // -----------------------------------------------------------------------
  // Texture resource management
  // -----------------------------------------------------------------------

  void GLBackend::CreateTexture(Texture* tex)
  {
    if (tex == nullptr)
    {
      return;
    }

    auto glData              = std::make_shared<GLTextureData>();
    const TextureSettings& s = tex->Settings();

    // DepthTexture — backed by a renderbuffer
    if (DepthTexture* dt = tex->As<DepthTexture>())
    {
      glGenRenderbuffers(1, &glData->textureId);
      glBindRenderbuffer(GL_RENDERBUFFER, glData->textureId);

      GLenum fmt = ToGLGraphicType(dt->GetDepthFormat());
      if (s.msaaCount > MsaaSampleCount::x0)
      {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, (int) s.msaaCount, fmt, tex->m_width, tex->m_height);
      }
      else
      {
        glRenderbufferStorage(GL_RENDERBUFFER, fmt, tex->m_width, tex->m_height);
      }

      glData->isRenderbuffer = true;
      tex->m_gpuData         = std::move(glData);
      return;
    }

    // RenderTarget MSAA — also a renderbuffer
    if (s.msaaCount > MsaaSampleCount::x0 && s.Target == GraphicTypes::Target2D)
    {
      glGenRenderbuffers(1, &glData->textureId);
      glBindRenderbuffer(GL_RENDERBUFFER, glData->textureId);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                       (int) s.msaaCount,
                                       ToGLGraphicType(s.InternalFormat),
                                       tex->m_width,
                                       tex->m_height);
      glData->isRenderbuffer = true;
      tex->m_gpuData         = std::move(glData);
      return;
    }

    // All other textures — regular GL texture
    glGenTextures(1, &glData->textureId);
    BindTextureDirect(ToGLGraphicType(s.Target), glData->textureId, 0);

    if (s.Target == GraphicTypes::Target2D)
    {
      // Texture or RenderTarget (non-MSAA 2D)
      void* data = tex->m_imagef ? (void*) tex->m_imagef : (void*) tex->m_image;
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   (GLint) ToGLGraphicType(s.InternalFormat),
                   tex->m_width,
                   tex->m_height,
                   0,
                   ToGLGraphicType(s.Format),
                   ToGLGraphicType(s.Type),
                   data);
    }
    else if (s.Target == GraphicTypes::TargetCubeMap)
    {
      // CubeMap — check if it has 6 loaded images
      if (CubeMap* cm = tex->As<CubeMap>())
      {
        const auto& images = cm->m_images;
        if (images.size() == 6)
        {
          uint sides[6] = {GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                           GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                           GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                           GL_TEXTURE_CUBE_MAP_NEGATIVE_Z};
          for (int i = 0; i < 6; i++)
          {
            glTexImage2D(sides[i], 0, GL_RGBA, tex->m_width, tex->m_width, 0, GL_RGBA, GL_UNSIGNED_BYTE, images[i]);
          }
        }
        else
        {
          // RenderTarget cube map — allocate empty faces
          for (uint i = 0; i < 6; i++)
          {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                         0,
                         (GLint) ToGLGraphicType(s.InternalFormat),
                         tex->m_width,
                         tex->m_height,
                         0,
                         ToGLGraphicType(s.Format),
                         ToGLGraphicType(s.Type),
                         nullptr);
          }
        }
      }
      else
      {
        // Generic cubemap (e.g. RenderTarget with TargetCubeMap)
        for (uint i = 0; i < 6; i++)
        {
          glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                       0,
                       (GLint) ToGLGraphicType(s.InternalFormat),
                       tex->m_width,
                       tex->m_height,
                       0,
                       ToGLGraphicType(s.Format),
                       ToGLGraphicType(s.Type),
                       nullptr);
        }
      }
    }
    else if (s.Target == GraphicTypes::Target2DArray)
    {
      assert(s.Layers > 0 && "Layer count must be at least 1");
      glTexImage3D(GL_TEXTURE_2D_ARRAY,
                   0,
                   (GLint) ToGLGraphicType(s.InternalFormat),
                   tex->m_width,
                   tex->m_height,
                   s.Layers,
                   0,
                   ToGLGraphicType(s.Format),
                   ToGLGraphicType(s.Type),
                   nullptr);
    }

    tex->m_gpuData = std::move(glData);
  }

  void GLBackend::DestroyTexture(Texture* tex)
  {
    GLTextureData* gl = tex ? GetGLTextureData(tex) : nullptr;
    if (gl == nullptr || gl->textureId == 0)
    {
      return;
    }

    if (gl->isRenderbuffer)
    {
      glDeleteRenderbuffers(1, &gl->textureId);
    }
    else
    {
      InvalidateTextureCache(gl->textureId);
      glDeleteTextures(1, &gl->textureId);
    }

    tex->m_gpuData.reset();
  }
  void GLBackend::ApplyTextureSettings(Texture* tex)
  {
    if (tex == nullptr)
    {
      return;
    }

    const TextureSettings& s = tex->Settings();
    GLenum target            = ToGLGraphicType(s.Target);

    GLTextureData* gl = GetGLTextureData(tex);
    if (gl == nullptr || gl->textureId == 0)
    {
      return;
    }
    BindTextureDirect(target, gl->textureId, 0);

    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, (GLint) ToGLGraphicType(s.MinFilter));
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, (GLint) ToGLGraphicType(s.MagFilter));
    glTexParameteri(target, GL_TEXTURE_WRAP_S, (GLint) ToGLGraphicType(s.WarpS));
    glTexParameteri(target, GL_TEXTURE_WRAP_T, (GLint) ToGLGraphicType(s.WarpT));

    if (s.Target == GraphicTypes::TargetCubeMap)
    {
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, (GLint) ToGLGraphicType(s.WarpR));
    }

    // Anisotropic filtering for 2D textures
    if (s.Target == GraphicTypes::Target2D && TK_GL_EXT_texture_filter_anisotropic == 1)
    {
      float maxAniso = 1.0f;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);

      EngineSettings& engSettings = GetEngineSettings();
      int anisoVal = engSettings.m_graphics->GetAnisotropicTextureFilteringVal().GetValue<int>();
      float aniso  = glm::min(maxAniso, glm::max(1.0f, float(anisoVal)));
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    }
  }

  void GLBackend::GenerateMipmaps(Texture* tex)
  {
    GLTextureData* gl = tex ? GetGLTextureData(tex) : nullptr;
    if (gl == nullptr || gl->textureId == 0)
    {
      return;
    }

    BindTextureDirect(ToGLGraphicType(tex->Settings().Target), gl->textureId, 0);
    glGenerateMipmap(ToGLGraphicType(tex->Settings().Target));
  }

  void GLBackend::UpdateTextureRegion(Texture* tex, const void* data)
  {
    GLTextureData* gl = tex ? GetGLTextureData(tex) : nullptr;
    if (gl == nullptr || gl->textureId == 0)
    {
      return;
    }

    const TextureSettings& s = tex->Settings();
    BindTextureDirect(ToGLGraphicType(s.Target), gl->textureId, 0);

    glTexSubImage2D(ToGLGraphicType(s.Target),
                    0,
                    0,
                    0,
                    tex->m_width,
                    tex->m_height,
                    ToGLGraphicType(s.Format),
                    ToGLGraphicType(s.Type),
                    data);
  }

  void GLBackend::SetTextureMaxMipLevel(Texture* tex, int maxLevel)
  {
    GLTextureData* gl = tex ? GetGLTextureData(tex) : nullptr;
    if (gl == nullptr || gl->textureId == 0)
    {
      return;
    }

    GLenum target = ToGLGraphicType(tex->Settings().Target);
    BindTextureDirect(target, gl->textureId, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, maxLevel);
  }

  void GLBackend::AllocateCubemapMipStorage(Texture* tex)
  {
    GLTextureData* gl = tex ? GetGLTextureData(tex) : nullptr;
    if (gl == nullptr || gl->textureId == 0)
    {
      return;
    }

    const TextureSettings& s = tex->Settings();
    BindTextureDirect(GL_TEXTURE_CUBE_MAP, gl->textureId, 0);

    const int numMipLevels = tex->CalculateMipmapLevels();
    for (int mip = 1; mip < numMipLevels; mip++)
    {
      int mipW = glm::max(1, tex->m_width >> mip);
      int mipH = glm::max(1, tex->m_height >> mip);

      for (int face = 0; face < 6; face++)
      {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                     mip,
                     (GLint) ToGLGraphicType(s.InternalFormat),
                     mipW,
                     mipH,
                     0,
                     ToGLGraphicType(s.Format),
                     ToGLGraphicType(s.Type),
                     nullptr);
      }
    }
  }

  void GLBackend::CopyCubemapFaceFromFramebuffer(Texture* cubemap, int face, int mip, int width, int height,
                                                   Framebuffer* readFb, Framebuffer* writeFb)
  {
    StoreFboBindings();

    GLTextureData* gl = cubemap ? GetGLTextureData(cubemap) : nullptr;
    if (gl == nullptr || gl->textureId == 0)
    {
      RestoreFboBindings();
      return;
    }

    if (readFb)
    {
      BindFramebuffer(GL_READ_FRAMEBUFFER, GetGLFramebufferData(readFb)->fboId);
    }
    if (writeFb)
    {
      BindFramebuffer(GL_DRAW_FRAMEBUFFER, GetGLFramebufferData(writeFb)->fboId);
    }

    BindTextureDirect(GL_TEXTURE_CUBE_MAP, gl->textureId, 0);
    glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, 0, 0, 0, 0, width, height);

    RestoreFboBindings();
  }

  // -----------------------------------------------------------------------
  // Framebuffer resource management
  // -----------------------------------------------------------------------

  void GLBackend::CreateFramebuffer(Framebuffer* fb)
  {
    if (fb == nullptr)
    {
      return;
    }

    auto glData = std::make_shared<GLFramebufferData>();
    glGenFramebuffers(1, &glData->fboId);
    BindFramebuffer(GL_FRAMEBUFFER, glData->fboId);
    fb->m_gpuData = std::move(glData);
  }

  void GLBackend::DestroyFramebuffer(Framebuffer* fb)
  {
    GLFramebufferData* gl = fb ? GetGLFramebufferData(fb) : nullptr;
    if (gl == nullptr || gl->fboId == 0)
    {
      return;
    }

    InvalidateFboCache(gl->fboId);
    GLuint id = gl->fboId;
    glDeleteFramebuffers(1, &id);
    fb->m_gpuData.reset();
  }

  void GLBackend::AttachColorTarget(Framebuffer* fb, RenderTargetPtr rt, int attachment, int mip, int layer, int face)
  {
    if (fb == nullptr || rt == nullptr)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, GetGLFramebufferData(fb)->fboId);
    GLenum glAttachment = GL_COLOR_ATTACHMENT0 + attachment;
    GLuint texId        = GetGLTextureData(rt.get())->textureId;

    if (rt->Settings().msaaCount > MsaaSampleCount::x0)
    {
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, glAttachment, GL_RENDERBUFFER, texId);
    }
    else if (face >= 0)
    {
      glFramebufferTexture2D(GL_FRAMEBUFFER, glAttachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, texId, mip);
    }
    else if (layer >= 0)
    {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, glAttachment, texId, mip, layer);
    }
    else
    {
      glFramebufferTexture2D(GL_FRAMEBUFFER, glAttachment, GL_TEXTURE_2D, texId, mip);
    }

    SetDrawBuffers(fb);
    CheckFramebufferComplete(fb);
  }

  void GLBackend::DetachColorTarget(Framebuffer* fb, int attachment)
  {
    if (fb == nullptr || GetGLFramebufferData(fb)->fboId == 0)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, GetGLFramebufferData(fb)->fboId);
    GLenum glAttachment = GL_COLOR_ATTACHMENT0 + attachment;
    glFramebufferTexture2D(GL_FRAMEBUFFER, glAttachment, GL_TEXTURE_2D, 0, 0);
    SetDrawBuffers(fb);
  }

  void GLBackend::AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt)
  {
    if (fb == nullptr || dt == nullptr)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, GetGLFramebufferData(fb)->fboId);
    GLenum attachment = dt->m_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, GetGLTextureData(dt.get())->textureId);
    CheckFramebufferComplete(fb);
  }

  void GLBackend::DetachDepthTarget(Framebuffer* fb)
  {
    if (fb == nullptr || GetGLFramebufferData(fb)->fboId == 0)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, GetGLFramebufferData(fb)->fboId);
    // Detach both possible depth attachment types
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
  }

  void GLBackend::SetDrawBuffers(Framebuffer* fb)
  {
    if (fb == nullptr || GetGLFramebufferData(fb)->fboId == 0)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, GetGLFramebufferData(fb)->fboId);

    GLenum colorAttachments[8] = {GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE};
    int maxAttachment          = -1;

    for (int i = 0; i < Framebuffer::m_maxColorAttachmentCount; i++)
    {
      RenderTargetPtr rt = fb->GetColorAttachment((Framebuffer::Attachment) i);
      if (rt != nullptr && rt->m_gpuData != nullptr)
      {
        colorAttachments[i] = GL_COLOR_ATTACHMENT0 + i;
        maxAttachment       = i;
      }
    }

    if (maxAttachment >= 0)
    {
      glDrawBuffers(maxAttachment + 1, colorAttachments);
    }
    else
    {
      glDrawBuffers(0, nullptr);
    }
  }

  void GLBackend::CheckFramebufferComplete(Framebuffer* fb)
  {
    if (fb == nullptr || GetGLFramebufferData(fb)->fboId == 0)
    {
      return;
    }

    BindFramebuffer(GL_FRAMEBUFFER, GetGLFramebufferData(fb)->fboId);
    GLenum check = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert(check == GL_FRAMEBUFFER_COMPLETE && "Framebuffer incomplete");
  }

  void GLBackend::ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments)
  {
    const int srcWidth  = src->GetSettings().width;
    const int srcHeight = src->GetSettings().height;
    const int dstWidth  = dst->GetSettings().width;
    const int dstHeight = dst->GetSettings().height;

    for (int atc : attachments)
    {
      using Attachment   = Framebuffer::Attachment;
      Attachment atcEnum = (Attachment) ((int) Attachment::ColorAttachment0 + atc);

      RenderTargetPtr srcRt = src->GetColorAttachment(atcEnum);
      assert(srcRt && "Trying to resolve a non existing attachment.");

      RenderTargetPtr targetRt = dst->GetColorAttachment(atcEnum);
      if (targetRt == nullptr)
      {
        TextureSettings settings = srcRt->Settings();
        settings.msaaCount       = MsaaSampleCount::x0;
        targetRt                 = MakeNewPtr<RenderTarget>();
        targetRt->ReconstructIfNeeded(srcRt->m_width, srcRt->m_height, &settings);
        dst->SetColorAttachment(atcEnum, targetRt);
      }

      srcRt->m_resolvedTexture = targetRt;

      // Bind after SetColorAttachment, which may have changed framebuffer bindings.
      BindFramebuffer(GL_READ_FRAMEBUFFER, GetGLFramebufferData(src.get())->fboId);
      BindFramebuffer(GL_DRAW_FRAMEBUFFER, GetGLFramebufferData(dst.get())->fboId);

      GLenum attachment = GL_COLOR_ATTACHMENT0 + atc;
      glReadBuffer(attachment);
      glDrawBuffers(1, &attachment);
      glBlitFramebuffer(0, 0, srcWidth, srcHeight, 0, 0, dstWidth, dstHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // Restore target framebuffer's original draw buffer configuration.
    SetDrawBuffers(dst.get());
  }

  void GLBackend::CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields)
  {
    StoreFboBindings();

    uint width  = 0;
    uint height = 0;
    uint srcId  = 0;

    if (src)
    {
      const FramebufferSettings& fbs = src->GetSettings();
      width                          = fbs.width;
      height                         = fbs.height;
      srcId                          = GetGLFramebufferData(src.get())->fboId;
    }

    BindFramebuffer(GL_READ_FRAMEBUFFER, srcId);

    uint destId = 0;
    if (dst)
    {
      dst->ReconstructIfNeeded(width, height);
      destId = GetGLFramebufferData(dst.get())->fboId;
    }
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, destId);

    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, ToGLBitfield(fields), GL_NEAREST);

    RestoreFboBindings();
  }

  void GLBackend::BlitToScreen(FramebufferPtr src) {}

  void GLBackend::StartTimerQuery()
  {
    m_cpuTime = GetElapsedMilliSeconds();
#ifdef GL_TIME_ELAPSED_EXT
    if constexpr (TK_PLATFORM == PLATFORM::TKWindows)
    {
      if (!m_timerQueryActive && !m_timerQueryWaiting)
      {
        if (!m_gpuTimerQuery)
        {
          glGenQueries(1, &m_gpuTimerQuery);
        }
        glBeginQuery(GL_TIME_ELAPSED_EXT, m_gpuTimerQuery);
        m_timerQueryActive = true;
      }
    }
#endif
  }

  void GLBackend::EndTimerQuery()
  {
    float cpuTime = GetElapsedMilliSeconds();
    m_cpuTime     = cpuTime - m_cpuTime;

#ifdef GL_TIME_ELAPSED_EXT
    if constexpr (TK_PLATFORM == PLATFORM::TKWindows)
    {
      if (m_timerQueryActive)
      {
        glEndQuery(GL_TIME_ELAPSED_EXT);
        m_timerQueryActive  = false;
        m_timerQueryWaiting = true;
      }

      if (m_timerQueryWaiting)
      {
        GLuint available = 0;
        glGetQueryObjectuiv(m_gpuTimerQuery, GL_QUERY_RESULT_AVAILABLE, &available);

        if (available)
        {
          GLuint elapsedTime;
          glGetQueryObjectuiv(m_gpuTimerQuery, GL_QUERY_RESULT, &elapsedTime);
          m_gpuTime           = glm::max(1.0f, (float) (elapsedTime) / 1000000.0f);
          m_timerQueryWaiting = false;
        }
      }
    }
#endif
  }

  void GLBackend::GetElapsedTime(float& cpu, float& gpu)
  {
    cpu = m_cpuTime;
    gpu = m_gpuTime;
  }

  void GLBackend::InvalidateFboCache(uint id)
  {
    if (m_currentReadFboId == id)
    {
      m_currentReadFboId = (uint) -1;
    }
    if (m_currentDrawFboId == id)
    {
      m_currentDrawFboId = (uint) -1;
    }
  }

  void GLBackend::InvalidateTextureCache(uint id)
  {
    for (int i = 0; i < 32; i++)
    {
      if (m_textureSlotCache[i].textureID == id)
      {
        m_textureSlotCache[i].textureID = 0;
        m_textureSlotCache[i].target    = 0;
      }
    }
  }

  void GLBackend::BindFramebuffer(uint target, uint id)
  {
    if (target == GL_FRAMEBUFFER)
    {
      if (m_currentReadFboId != id || m_currentDrawFboId != id)
      {
        glBindFramebuffer(GL_FRAMEBUFFER, id);
        m_currentReadFboId = id;
        m_currentDrawFboId = id;
      }
      return;
    }

    if (target == GL_READ_FRAMEBUFFER && m_currentReadFboId != id)
    {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, id);
      m_currentReadFboId = id;
    }

    if (target == GL_DRAW_FRAMEBUFFER && m_currentDrawFboId != id)
    {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, id);
      m_currentDrawFboId = id;
    }
  }

  void GLBackend::BindTextureDirect(uint target, uint id, uint slot)
  {
    if (slot >= 32)
    {
      return;
    }

    if (m_textureSlotCache[slot].textureID != id || m_textureSlotCache[slot].target != target)
    {
      glActiveTexture(GL_TEXTURE0 + slot);
      glBindTexture(target, id);
      m_textureSlotCache[slot].textureID = id;
      m_textureSlotCache[slot].target    = target;
    }
  }

  void GLBackend::BindVAO(uint vao)
  {
    if (m_currentVAO != vao)
    {
      glBindVertexArray(vao);
      m_currentVAO = vao;
    }
  }

  // Phase 7a: Custom uniforms and renderer utility
  //////////////////////////////////////////

  void GLBackend::SubmitCustomUniforms(const GpuProgramPtr& program,
                                       std::unordered_map<String, ShaderUniform>& uniforms)
  {
    for (auto& [name, uniform] : uniforms)
    {
      GLint loc = program->GetCustomUniformLocation(uniform);
      switch (uniform.GetType())
      {
        case ShaderUniform::UniformType::Bool:
          glUniform1ui(loc, uniform.GetVal<bool>());
          break;
        case ShaderUniform::UniformType::Float:
          glUniform1f(loc, uniform.GetVal<float>());
          break;
        case ShaderUniform::UniformType::Int:
          glUniform1i(loc, uniform.GetVal<int>());
          break;
        case ShaderUniform::UniformType::UInt:
          glUniform1ui(loc, uniform.GetVal<uint>());
          break;
        case ShaderUniform::UniformType::Vec2:
          glUniform2fv(loc, 1, reinterpret_cast<float*>(&uniform.GetVal<Vec2>()));
          break;
        case ShaderUniform::UniformType::Vec3:
          glUniform3fv(loc, 1, reinterpret_cast<float*>(&uniform.GetVal<Vec3>()));
          break;
        case ShaderUniform::UniformType::Vec4:
          glUniform4fv(loc, 1, reinterpret_cast<float*>(&uniform.GetVal<Vec4>()));
          break;
        case ShaderUniform::UniformType::Mat3:
          glUniformMatrix3fv(loc, 1, false, reinterpret_cast<float*>(&uniform.GetVal<Mat3>()));
          break;
        case ShaderUniform::UniformType::Mat4:
          glUniformMatrix4fv(loc, 1, false, reinterpret_cast<float*>(&uniform.GetVal<Mat4>()));
          break;
        default:
          assert(false && "Invalid uniform type.");
          break;
      }
    }
  }

  void GLBackend::SetUniform4f(int location, const Vec4& value)
  {
    glUniform4f(location, value.x, value.y, value.z, value.w);
  }

  String GLBackend::GetBackendRendererString()
  {
    const char* renderer = (const char*) glGetString(GL_RENDERER);
    return renderer ? String(renderer) : String("Unknown");
  }

  int GLBackend::GetMaxArrayTextureLayers()
  {
    GLint maxLayers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
    return (int) maxLayers;
  }

  void GLBackend::SetSrgbAutoEncoding(bool enable)
  {
#ifdef GL_FRAMEBUFFER_SRGB
    const int glSrgbFlag = GL_FRAMEBUFFER_SRGB;
#elif defined(GL_FRAMEBUFFER_SRGB_EXT)
    const int glSrgbFlag = GL_FRAMEBUFFER_SRGB_EXT;
#else
    const int glSrgbFlag = 0;
#endif

    if constexpr (glSrgbFlag)
    {
      if (enable)
      {
        glEnable(glSrgbFlag);
      }
      else
      {
        glDisable(glSrgbFlag);
      }
    }
  }

  void GLBackend::Finish()
  {
    glFinish();
  }

  void GLBackend::SetDefaultClearColor(const Vec4& color)
  {
    glClearColor(color.x, color.y, color.z, color.w);
  }

  bool GLBackend::ValidateBackbufferSrgbEncoding()
  {
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glViewport(0, 0, (GLsizei) 100, (GLsizei) 100);

    const float testLinear = 0.5f;
    glClearColor(testLinear, testLinear, testLinear, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    ubyte rgba[4] = {0, 0, 0, 0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    ubyte expected = 188;
    int tolerance  = 2;
    bool matchR    = std::abs((int) rgba[0] - (int) expected) <= tolerance;
    bool matchG    = std::abs((int) rgba[1] - (int) expected) <= tolerance;
    bool matchB    = std::abs((int) rgba[2] - (int) expected) <= tolerance;

    return matchR && matchG && matchB;
  }

  void GLBackend::EnableScissorTest(bool enable)
  {
    if (enable)
    {
      glEnable(GL_SCISSOR_TEST);
    }
    else
    {
      glDisable(GL_SCISSOR_TEST);
    }
  }

  void GLBackend::ReadPixels(int x, int y, int w, int h,
                             GraphicTypes format, GraphicTypes type, void* data)
  {
    glReadPixels(x, y, w, h, ToGLGraphicType(format), ToGLGraphicType(type), data);
  }

  void GLBackend::UpdateTextureSubRegion(Texture* tex, int x, int y, int w, int h, const void* data)
  {
    GLTextureData* gl = tex ? GetGLTextureData(tex) : nullptr;
    if (gl == nullptr || gl->textureId == 0)
    {
      return;
    }

    const TextureSettings& s = tex->Settings();
    BindTextureDirect(ToGLGraphicType(s.Target), gl->textureId, 0);
    glTexSubImage2D(ToGLGraphicType(s.Target), 0, x, y, w, h, ToGLGraphicType(s.Format), ToGLGraphicType(s.Type), data);
  }

  void GLBackend::PushDebugGroup(StringView name)
  {
    if (glPushGroupMarkerEXT != nullptr)
    {
      glPushGroupMarkerEXT(-1, name.data());
    }
  }

  void GLBackend::PopDebugGroup()
  {
    if (glPopGroupMarkerEXT != nullptr)
    {
      glPopGroupMarkerEXT();
    }
  }

  bool GLBackend::SupportsFloatTextureLinearFilter()
  {
    return TK_GL_OES_texture_float_linear != 0;
  }

  void* GLBackend::GetNativeTextureHandle(Texture* tex)
  {
    GLTextureData* gl = tex ? GetGLTextureData(tex) : nullptr;
    return gl ? (void*) (intptr_t) gl->textureId : nullptr;
  }

} // namespace ToolKit
