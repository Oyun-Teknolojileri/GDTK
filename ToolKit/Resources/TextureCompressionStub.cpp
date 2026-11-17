/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

/**
 * @file TextureCompressionStub.cpp
 * Web and Android replacement for TextureCompression.cpp.
 *
 * The encoder is an authoring feature: Import and Packer are desktop executables, and a game payload
 * has no use for a few megabytes of block encoder. `ToolKit/CMakeLists.txt` picks this file instead
 * of the real one on those platforms, so `FileManager` and anything else that calls the API needs no
 * preprocessor guard, and a build that somehow does reach it fails loudly instead of silently
 * shipping uncompressed textures.
 */

#include "TextureCompression.h"

#include "Logger.h"

namespace ToolKit
{

  namespace
  {
    void WarnOnce()
    {
      static bool warned = false;
      if (!warned)
      {
        warned = true;
        TK_WRN("Texture compression is a desktop tool feature; this build cannot encode KTX2.");
      }
    }
  } // namespace

  bool CompressImageToKtx2(const ubyte*,
                           int,
                           int,
                           const TextureCompressionOptions&,
                           std::vector<ubyte>& outKtx2)
  {
    outKtx2.clear();
    WarnOnce();
    return false;
  }

  bool CompressImageTopDownToKtx2(const ubyte*,
                                  int,
                                  int,
                                  const TextureCompressionOptions&,
                                  std::vector<ubyte>& outKtx2)
  {
    outKtx2.clear();
    WarnOnce();
    return false;
  }

  bool WriteKtx2(const String&, const std::vector<ubyte>&)
  {
    WarnOnce();
    return false;
  }

  bool CompressFileToMemory(const String&, const TextureCompressionOptions&, std::vector<ubyte>& outKtx2)
  {
    outKtx2.clear();
    WarnOnce();
    return false;
  }

  bool CompressFileToKtx2(const String&, const String&, const TextureCompressionOptions&)
  {
    WarnOnce();
    return false;
  }

  bool CompressFiles(const std::vector<TextureCompressionJob>&, int)
  {
    WarnOnce();
    return false;
  }

} // namespace ToolKit
