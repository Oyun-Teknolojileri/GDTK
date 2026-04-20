/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "IGraphicsBackend.h"
#include "Types.h"

namespace ToolKit
{

  /**
   * Generic gpu buffer for std140 layout data.
   * A uniform buffer should be used where dynamic indexing is not needed.
   * A uniform buffer update creates a gpu synchronization point, so it should not be updated frequently between draw
   * calls. Most suitable use case is per frame data such as camera transforms, frame count, elapsed time etc.
   */
  class TK_API UniformBuffer
  {
   public:
    UniformBuffer();
    virtual ~UniformBuffer();

    void Init(uint64 size);

    /**
     * Maps the cpu data to gpu.
     * data must be a struct or an array of struct with std140 layout.
     */
    void Map(const void* data, uint64 size);

   public:
    /** Slot corresponds the buffer's binding location. */
    int m_slot;
    /** Backend-owned GPU resource data. */
    GpuResourceDataPtr m_gpuData;

   public:
    uint64 m_size = 0;
  };

  template <typename DataLayout, int Slot>
  class TK_API GpuBufferBase
  {
   public:
    static constexpr int Binding() { return Slot; }

    void Init()
    {
      m_buffer.Init(sizeof(DataLayout));
      m_buffer.m_slot = Binding();
    }

    void Invalidate() { m_invalid = true; }
    bool IsValid() const { return !m_invalid; }
    UniformBuffer& GetBuffer() { return m_buffer; }

    void Map()
    {
      if (m_invalid)
      {
        m_buffer.Map(&m_data, sizeof(DataLayout));
        m_invalid = false;
      }
    }

   public:
    DataLayout m_data;

   private:
    UniformBuffer m_buffer;
    bool m_invalid = true;
  };

} // namespace ToolKit