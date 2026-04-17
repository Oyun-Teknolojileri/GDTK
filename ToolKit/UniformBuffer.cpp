/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "UniformBuffer.h"

#include "Renderer.h"
#include "RenderSystem.h"
#include "Stats.h"

#include "DebugNew.h"

namespace ToolKit
{

  static IGraphicsBackend* GetBackend()
  {
    return GetRenderSystem()->GetRenderer()->GetBackend();
  }

  UniformBuffer::UniformBuffer()
  {
    m_slot = -1;
    m_size = 0;
  }

  UniformBuffer::~UniformBuffer()
  {
    if (m_gpuData)
    {
      GetBackend()->DestroyUniformBuffer(this);
    }
  }

  void UniformBuffer::Init(uint64 size)
  {
    GetBackend()->CreateUniformBuffer(this, size);
  }

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

    GetBackend()->UpdateUniformBuffer(this, data, size);
  }

} // namespace ToolKit