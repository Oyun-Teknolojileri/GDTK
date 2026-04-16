/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "RHI.h"

#include "GLBackend.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "TKOpenGL.h"

#include "DebugNew.h"

namespace ToolKit
{
  uint RHI::m_currentReadFramebufferID = -1;
  uint RHI::m_currentDrawFramebufferID = -1;
  uint RHI::m_currentVAO               = -1;

  IntArray RHI::m_storedReadFramebufferStack;
  IntArray RHI::m_storedDrawFramebufferStack;

  // Helper to get the active backend.
  static GLBackend* GetBackend()
  {
    if (RenderSystem* rsys = GetRenderSystem())
    {
      if (Renderer* renderer = rsys->GetRenderer())
      {
        return static_cast<GLBackend*>(renderer->GetBackend());
      }
    }
    return nullptr;
  }

  void RHI::SetFramebuffer(uint target, uint framebufferID)
  {
    if (GLBackend* backend = GetBackend())
    {
      backend->BindFramebuffer(target, framebufferID);
    }
  }

  void RHI::DeleteFramebuffers(int n, const uint* framebuffers)
  {
    if (GLBackend* backend = GetBackend())
    {
      for (int i = 0; i < n; ++i)
      {
        backend->InvalidateFboCache(framebuffers[i]);
      }
    }
  }

  void RHI::StoreFramebufferBindings()
  {
    if (GLBackend* backend = GetBackend())
    {
      backend->StoreFboBindings();
    }
  }

  void RHI::RestoreFramebufferBindings()
  {
    if (GLBackend* backend = GetBackend())
    {
      backend->RestoreFboBindings();
    }
  }

  void RHI::SetTexture(uint target, uint textureID, uint textureSlot)
  {
    if (GLBackend* backend = GetBackend())
    {
      backend->BindTextureDirect(target, textureID, textureSlot);
    }
  }

  void RHI::DeleteTexture(uint textureID)
  {
    if (GLBackend* backend = GetBackend())
    {
      backend->InvalidateTextureCache(textureID);
    }
    // NOTE: actual glDeleteTextures is now called by GLBackend::DestroyTexture.
    // This path is only reached from legacy call sites that still use raw IDs
    // (e.g. Renderer utility methods). Those will be cleaned up in a later phase.
    glDeleteTextures(1, (const GLuint*) &textureID);
  }

  void RHI::BindVertexArray(uint VAO)
  {
    if (GLBackend* backend = GetBackend())
    {
      backend->BindVAO(VAO);
    }
  }

} // namespace ToolKit
