/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
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
    // Dependent-base alias: `StructBuffer<Struct>` is dependent on `Struct`, so its members must
    // be reached through a type (not the bare template name `StructBuffer`, which GCC 16's
    // template-body checking rejects as an ambiguous qualified-id).
    using BaseType = StructBuffer<Struct>;

   public:
    /** Maps the current data to gpu buffer. */
    void Map()
    {
      if (m_buffer && m_buffer->m_initiated)
      {
        BaseType::Map([this](void* data, uint64 /*size*/) -> void { m_buffer->Map(data); });
      }
      else
      {
        TK_ERR("DrawBuffer is not initialized. Use Resize to get a valid buffer.");
      }
    }

    /** Initialize and sets the size of underlying buffer. */
    void Resize(int count)
    {
      BaseType::Allocate(count);

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
      sets.Format         = format;
      sets.InternalFormat = format;            // e.g. FormatRGBA32F — must match Format for data textures
      sets.Type           = GraphicTypes::TypeFloat;
      sets.MinFilter      = GraphicTypes::SampleNearest; // texelFetch ignores filtering, but safe
      sets.MagFilter      = GraphicTypes::SampleNearest;
      m_buffer            = MakeNewPtr<DataTexture>(width, height, sets, "DrawBuffer");

      // Init the GPU resource with the initial CPU data so m_initiated is true and
      // subsequent Map() calls upload correctly (Map no-ops when m_initiated == false).
      m_buffer->Init(this->m_data.data());
    }

    /** Indexed access to the CPU-side row (write/mutate before `Map()`). */
    Struct& operator[](int i) { return this->m_data[i]; }
    const Struct& operator[](int i) const { return this->m_data[i]; }

   public:
    /** Gpu buffer that holds the draw data. */
    DataTexturePtr m_buffer;
  };

} // namespace ToolKit