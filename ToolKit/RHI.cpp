/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "RHI.h"

#include "DebugNew.h"

namespace ToolKit
{
  GLuint RHI::m_currentReadFramebufferID = UINT_MAX;
  GLuint RHI::m_currentDrawFramebufferID = UINT_MAX;
  GLuint RHI::m_currentFramebufferID     = UINT_MAX;
  GLuint RHI::m_currentVAO               = UINT_MAX;
  RHI::TextureIdSlotMap RHI::m_textureIdSlotMap;

  void RHI::SetFramebuffer(GLenum target, GLuint framebufferID)
  {
    if (target == GL_READ_FRAMEBUFFER)
    {
      if (m_currentReadFramebufferID == framebufferID)
      {
        return;
      }
      else
      {
        m_currentReadFramebufferID = framebufferID;
      }
    }
    else if (target == GL_DRAW_FRAMEBUFFER)
    {
      if (m_currentDrawFramebufferID == framebufferID)
      {
        return;
      }
      else
      {
        m_currentDrawFramebufferID = framebufferID;
      }
    }
    else
    {
      if (m_currentFramebufferID == framebufferID && m_currentReadFramebufferID == framebufferID &&
          m_currentDrawFramebufferID == framebufferID)
      {
        return;
      }
      else
      {
        m_currentFramebufferID     = framebufferID;
        m_currentReadFramebufferID = framebufferID;
        m_currentDrawFramebufferID = framebufferID;
      }
    }

    glBindFramebuffer(target, framebufferID);
  }

  void RHI::DeleteFramebuffers(GLsizei n, const GLuint* framebuffers)
  {
    glDeleteFramebuffers(n, framebuffers);

    for (int i = 0; i < n; ++i)
    {
      if (framebuffers[i] == m_currentFramebufferID)
      {
        m_currentFramebufferID = -1;
      }

      if (framebuffers[i] == m_currentReadFramebufferID)
      {
        m_currentReadFramebufferID = -1;
      }

      if (framebuffers[i] == m_currentDrawFramebufferID)
      {
        m_currentDrawFramebufferID = -1;
      }
    }
  }

  void RHI::InvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum* attachments)
  {
    glInvalidateFramebuffer(target, numAttachments, attachments);
  }

  void RHI::SetTexture(GLenum target, GLuint textureID, GLenum textureSlot)
  {
    assert(textureSlot >= 0 && textureSlot <= 31);

    if (m_textureIdSlotMap[textureSlot] != textureID)
    {
      glActiveTexture(GL_TEXTURE0 + textureSlot);
      glBindTexture(target, textureID);

      m_textureIdSlotMap[textureSlot] = textureID;
    }
  }

  void RHI::DeleteTexture(GLuint textureID)
  {
    // iterate over all slots and reset texture id if it matches given texture
    for (auto& it : m_textureIdSlotMap)
    {
      if (it.second == textureID)
      {
        it.second = -1;
      }
    }

    glDeleteTextures(1, &textureID);
  }

  void RHI::BindVertexArray(GLuint VAO)
  {
    if (m_currentVAO != VAO)
    {
      glBindVertexArray(VAO);

      m_currentVAO = VAO;
    }
  }

} // namespace ToolKit
