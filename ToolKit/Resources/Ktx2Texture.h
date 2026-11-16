/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file Ktx2Texture.h
 * Reading and transcoding KTX2 textures (Basis Universal containers).
 *
 * The container holds either ETC1S or UASTC supercompressed data and is transcoded on the CPU into
 * whatever block format the backend can upload, or into plain RGBA8 when it can upload none. Nothing
 * here touches the GPU: Texture::Load calls into this file from the loading thread, and Texture::Init
 * uploads the result.
 */

#include "Types.h"

namespace ToolKit
{

  /** Header facts about a KTX2 container, without transcoding anything. */
  struct Ktx2Info
  {
    int Width      = 0;
    int Height     = 0;
    int LevelCount = 0;
    bool HasAlpha  = false;
    bool IsSrgb    = false;
    bool IsHdr     = false;
  };

  /** True when the buffer starts with the KTX2 identifier ("<<KTX 20>>\r\n\x1A\n"). */
  TK_API bool IsKtx2Data(const ubyte* data, size_t size);

  /**
   * Reads the container header. Cheap: no slice is decompressed.
   * @param data Buffer holding the whole file.
   * @param size Buffer size in bytes.
   * @param info Receives the header facts.
   * @return False when the container cannot be parsed.
   */
  TK_API bool Ktx2ReadInfo(const ubyte* data, size_t size, Ktx2Info& info);

  /**
   * Transcodes every mip level of the container to the given block format.
   *
   * @param data Buffer holding the whole file.
   * @param size Buffer size in bytes.
   * @param format Target block layout. Must not be CompressedFormat::None.
   * @param levels Receives one entry per level, largest first. Empty on failure.
   * @return False when the container is not readable or a level fails to transcode.
   */
  TK_API bool Ktx2Transcode(const ubyte* data,
                            size_t size,
                            CompressedFormat format,
                            std::vector<TextureLevel>& levels);

  /**
   * Transcodes every mip level to 8 bit RGBA. This is the fallback for a device or a build that can
   * not upload a block compressed format, and it is what the engine did before KTX2 existed.
   *
   * The returned level data is cropped to the source dimensions: the transcoder pads to whole 4x4
   * blocks, and a texture whose width is not a multiple of 4 must still upload at its own size.
   *
   * @param data Buffer holding the whole file.
   * @param size Buffer size in bytes.
   * @param levels Receives one entry per level, largest first. Empty on failure.
   * @return False when the container is not readable or a level fails to transcode.
   */
  TK_API bool Ktx2DecodeRgba8(const ubyte* data, size_t size, std::vector<TextureLevel>& levels);

} // namespace ToolKit
