/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "UniformBuffer.h"

#include "RHI.h"
#include "TKOpenGL.h"

#include "DebugNew.h"

namespace ToolKit
{

  UniformBuffer::UniformBuffer()
  {
    m_id   = 0;
    m_slot = -1;
  }

  UniformBuffer::~UniformBuffer() { glDeleteBuffers(1, &m_id); }

  void UniformBuffer::Init(uint64 size)
  {
    m_size = size;
    glGenBuffers(1, &m_id);
    glBindBuffer(GL_UNIFORM_BUFFER, m_id);
    glBufferData(GL_UNIFORM_BUFFER, m_size, nullptr, GL_DYNAMIC_DRAW);
  }

  void UniformBuffer::Map(const void* data, uint64 size)
  {
    // Sanitize buffer.
    if (m_id == NullHandle || m_slot == InvalidHandle)
    {
      TK_ERR("Uniform buffer is not initialized properly.");
      return;
    }

    if (size != m_size)
    {
      TK_ERR("Uniform buffer size does not match.");
      return;
    }

    if (size == 0)
    {
      return;
    }

    if (TKStats* tkStats = GetTKStats())
    {
      tkStats->m_uboUpdatesPerFrame++;
    }

    glBindBuffer(GL_UNIFORM_BUFFER, m_id);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, size, data);
  }

} // namespace ToolKit