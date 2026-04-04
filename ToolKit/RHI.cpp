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
  GLuint RHI::m_currentReadFramebufferID = -1;
  GLuint RHI::m_currentDrawFramebufferID = -1;
  GLuint RHI::m_currentVAO               = -1;

  IntArray RHI::m_storedReadFramebufferStack;
  IntArray RHI::m_storedDrawFramebufferStack;

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
      if (m_currentReadFramebufferID == framebufferID && m_currentDrawFramebufferID == framebufferID)
      {
        return;
      }
      else
      {
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

  void RHI::StoreFramebufferBindings()
  {
    m_storedReadFramebufferStack.push_back(m_currentReadFramebufferID);
    m_storedDrawFramebufferStack.push_back(m_currentDrawFramebufferID);
  }

  void RHI::RestoreFramebufferBindings()
  {
    // Ensure there is a stored state to restore
    if (m_storedReadFramebufferStack.empty() || m_storedDrawFramebufferStack.empty())
    {
      // Nothing to restore.
      assert(false && "RestoreFramebufferBindings called without a matching StoreFramebufferBindings.");
      return;
    }

    GLuint readId = m_storedReadFramebufferStack.back();
    GLuint drawId = m_storedDrawFramebufferStack.back();

    m_storedReadFramebufferStack.pop_back();
    m_storedDrawFramebufferStack.pop_back();

    SetFramebuffer(GL_READ_FRAMEBUFFER, readId);
    SetFramebuffer(GL_DRAW_FRAMEBUFFER, drawId);
  }

  void RHI::SetTexture(GLenum target, GLuint textureID, GLenum textureSlot)
  {
    assert(textureSlot >= 0 && textureSlot <= 31);

    TextureSlotState& state = m_textureIdSlotMap[textureSlot];
    if (state.textureID != textureID || state.target != target)
    {
      glActiveTexture(GL_TEXTURE0 + textureSlot);
      glBindTexture(target, textureID);

      state.textureID = textureID;
      state.target    = target;
    }
  }

  void RHI::DeleteTexture(GLuint textureID)
  {
    for (auto& it : m_textureIdSlotMap)
    {
      if (it.second.textureID == textureID)
      {
        it.second.textureID = 0;
        it.second.target    = 0;
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
