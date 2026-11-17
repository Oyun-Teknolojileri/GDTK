/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file DdsLoader.h Header for the DDS decoding used by the import CLI.
 */

#include <Types.h>

namespace ToolKit
{

  /**
   * Decodes the first mip level of a DDS file into 8 bit RGBA.
   *
   * The engine decodes images with stb_image, which has no DDS reader, so a DDS reference an FBX
   * points at can never become a usable Texture. The import CLI converts such a reference on the
   * way in. Only the block compressed formats asset packages ship are implemented: BC1 (DXT1),
   * BC3 (DXT5) and BC5 (ATI2).
   *
   * The pixels come back in file order (first row is the top row), the same order stb_image hands
   * back for a PNG, so a caller that encodes them goes through the same flip as the file based
   * encode entry points.
   *
   * @param ddsFile Source DDS file.
   * @param rgba Receives width * height * 4 bytes.
   * @param width Receives the image width.
   * @param height Receives the image height.
   * @return True when the file was decoded.
   */
  bool DecodeDdsToRgba(const String& ddsFile, std::vector<ubyte>& rgba, int& width, int& height);

  /**
   * Decodes the first mip level of a DDS file and writes it out as an RGBA8 PNG.
   *
   * This is the fallback for a texture that could not be encoded to KTX2: the material still has to
   * point at a file the engine can load.
   *
   * @param ddsFile Source DDS file.
   * @param pngFile Destination PNG file. The parent folder must exist.
   * @return True when the file was decoded and written.
   */
  bool ConvertDdsToPng(const String& ddsFile, const String& pngFile);

} // namespace ToolKit
