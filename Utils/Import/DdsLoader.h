/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file DdsLoader.h Header for the DDS to PNG conversion used by the import CLI.
 */

#include <Types.h>

namespace ToolKit
{

  /**
   * Decodes the first mip level of a DDS file and writes it out as an RGBA8 PNG.
   *
   * The engine decodes images with stb_image, which has no DDS reader, so a DDS reference an FBX
   * points at can never become a usable Texture. The import CLI converts such a reference on the
   * way in, which leaves the material pointing at a file the engine can load. Only the block
   * compressed formats asset packages ship are implemented: BC1 (DXT1), BC3 (DXT5) and BC5 (ATI2).
   *
   * @param ddsFile Source DDS file.
   * @param pngFile Destination PNG file. The parent folder must exist.
   * @return True when the file was decoded and written.
   */
  bool ConvertDdsToPng(const String& ddsFile, const String& pngFile);

} // namespace ToolKit
