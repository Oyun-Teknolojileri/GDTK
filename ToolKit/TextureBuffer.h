/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "GenericBuffers.h"
#include "RHI.h"
#include "Texture.h"

namespace ToolKit
{

  /** Generic gpu buffer that uses a texture to store array of structs. */
  template <typename Struct, GraphicTypes format>
  class TK_API TextureBuffer : public StructBuffer<Struct>
  {
   public:
    /** Maps the current data to gpu buffer. */
    void Map()
    {
      if (m_buffer && m_buffer->m_initiated)
      {
        StructBuffer::Map([this](void* data, uint64 size) -> void { m_buffer->Map(data, size); });
      }
      else
      {
        TK_ERR("DrawBuffer is not initialized. Use Resize to get a valid buffer.");
      }
    }

    /** Initialize and sets the size of underlying buffer. */
    void Resize(int count)
    {
      StructBuffer::Allocate(count);

      // Calculate the size of the buffer in bytes.
      const int structSizeBytes = sizeof(Struct);
      const int bytesPerPixel   = BytesOfFormat(format);
      const int vec4PerStruct   = (structSizeBytes + bytesPerPixel - 1) / bytesPerPixel; // ceil division

      const int totalPixels     = count * vec4PerStruct;

      // Layout the 2D texture (safe max width)
      const int maxTextureWidth = 1024;
      int width                 = std::min(totalPixels, maxTextureWidth);
      int height                = (totalPixels + width - 1) / width; // ceiling division

      // Create a texture
      TextureSettings sets;
      sets.Format = format;
      m_buffer    = MakeNewPtr<DataTexture>(width, height, sets, "DrawBuffer");
    }

   public:
    /** Gpu buffer that holds the draw data. */
    DataTexturePtr m_buffer;
  };

} // namespace ToolKit