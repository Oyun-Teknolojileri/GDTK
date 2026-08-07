/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "UniformBuffer.h"

#include "RenderSystem.h"
#include "Renderer.h"
#include "Stats.h"

#include "DebugNew.h"

namespace ToolKit
{

  UniformBuffer::UniformBuffer()
  {
    m_slot = -1;
    m_size = 0;
  }

  UniformBuffer::~UniformBuffer()
  {
    if (m_gpuData)
    {
      GetRenderSystem()->GetBackend()->DestroyUniformBuffer(this);
    }
  }

  void UniformBuffer::Destroy()
  {
    if (m_gpuData)
    {
      GetRenderSystem()->GetBackend()->DestroyUniformBuffer(this);
      m_gpuData.reset();
    }
    m_slot = -1;
    m_size = 0;
  }

  void UniformBuffer::Init(uint64 size) { GetRenderSystem()->GetBackend()->CreateUniformBuffer(this, size); }

  void UniformBuffer::Map(const void* data, uint64 size)
  {
    if (m_gpuData == nullptr || m_slot == InvalidHandle)
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

    Stats::IncrementStat(FrameStatType::UboUpdates);

    GetRenderSystem()->GetBackend()->UpdateUniformBuffer(this, data, size);
  }

} // namespace ToolKit
