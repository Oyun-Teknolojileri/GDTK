/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Ktx2Texture.h"

#include "Logger.h"

#include <cstring>
#include <mutex>

#include "basisu_transcoder.h"

namespace ToolKit
{

  namespace
  {
    // KTX2 identifier: 0xAB 'K' 'T' 'X' ' ' '2' '0' 0xBB '\r' '\n' 0x1A '\n'.
    const ubyte Ktx2Identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

    /** The transcoder is not thread safe on its own init; every entry point funnels through here. */
    void InitTranscoder()
    {
      static std::once_flag once;
      std::call_once(once,
                     []()
                     {
                       basist::basisu_transcoder_init();
                     });
    }

    /** Bytes of one 4x4 block for the given format. */
    int BlockBytesOf(CompressedFormat format)
    {
      switch (format)
      {
        case CompressedFormat::BC1:
        case CompressedFormat::ETC2_RGB:
          return 8;
        case CompressedFormat::BC3:
        case CompressedFormat::BC5:
        case CompressedFormat::BC7:
        case CompressedFormat::ETC2_RGBA:
        case CompressedFormat::ASTC_4x4:
          return 16;
        default:
          assert(false && "Not a block compressed format.");
          return 0;
      }
    }

    /**
     * Maps the engine's format to the transcoder's. ETC2_RGB asks for ETC1 blocks on purpose: ETC1
     * block data is a valid subset of an ETC2 RGB8 texture, which is how the opaque path reaches
     * GL_COMPRESSED_RGB8_ETC2 at half the size of the RGBA variant.
     */
    basist::transcoder_texture_format ToTranscoderFormat(CompressedFormat format)
    {
      using TF = basist::transcoder_texture_format;
      switch (format)
      {
        case CompressedFormat::BC1:
          return TF::cTFBC1_RGB;
        case CompressedFormat::BC3:
          return TF::cTFBC3_RGBA;
        case CompressedFormat::BC5:
          return TF::cTFBC5_RG;
        case CompressedFormat::BC7:
          return TF::cTFBC7_RGBA;
        case CompressedFormat::ETC2_RGB:
          return TF::cTFETC1_RGB;
        case CompressedFormat::ETC2_RGBA:
          return TF::cTFETC2_RGBA;
        case CompressedFormat::ASTC_4x4:
          return TF::cTFASTC_LDR_4x4_RGBA;
        default:
          return TF::cTFRGBA32;
      }
    }
  } // namespace

  bool IsKtx2Data(const ubyte* data, size_t size)
  {
    if (data == nullptr || size < sizeof(Ktx2Identifier))
    {
      return false;
    }

    return std::memcmp(data, Ktx2Identifier, sizeof(Ktx2Identifier)) == 0;
  }

  bool Ktx2ReadInfo(const ubyte* data, size_t size, Ktx2Info& info)
  {
    if (!IsKtx2Data(data, size))
    {
      return false;
    }

    InitTranscoder();

    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(data, (uint32_t) size))
    {
      TK_WRN("KTX2: the container header could not be parsed.");
      return false;
    }

    info.Width      = (int) transcoder.get_width();
    info.Height     = (int) transcoder.get_height();
    info.LevelCount = (int) transcoder.get_levels();
    info.HasAlpha   = transcoder.get_has_alpha();
    info.IsSrgb     = transcoder.is_srgb();
    info.IsHdr      = transcoder.is_hdr();
    return true;
  }

  bool Ktx2Transcode(const ubyte* data, size_t size, CompressedFormat format, std::vector<TextureLevel>& levels)
  {
    levels.clear();

    if (!IsKtx2Data(data, size) || format == CompressedFormat::None)
    {
      return false;
    }

    InitTranscoder();

    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(data, (uint32_t) size))
    {
      TK_WRN("KTX2: the container header could not be parsed.");
      return false;
    }

    // HDRI keeps its float path for now; a UASTC HDR container has no LDR blocks to give.
    if (transcoder.is_hdr())
    {
      TK_WRN("KTX2: HDR containers are not supported yet, keeping the texture unloaded.");
      return false;
    }

    if (!transcoder.start_transcoding())
    {
      TK_WRN("KTX2: start_transcoding failed, the container is unsupported or corrupt.");
      return false;
    }

    const basist::transcoder_texture_format target = ToTranscoderFormat(format);
    const int blockBytes                           = BlockBytesOf(format);
    const uint32_t levelCount                      = transcoder.get_levels();

    levels.resize(levelCount);

    for (uint32_t levelIndex = 0; levelIndex < levelCount; ++levelIndex)
    {
      basist::ktx2_image_level_info levelInfo;
      if (!transcoder.get_image_level_info(levelInfo, levelIndex, 0, 0))
      {
        TK_WRN("KTX2: no image info for mip level %d.", (int) levelIndex);
        levels.clear();
        return false;
      }

      TextureLevel& level = levels[levelIndex];
      level.Width         = (int) levelInfo.m_orig_width;
      level.Height        = (int) levelInfo.m_orig_height;

      // The block count, not the texel count: the transcoder always emits whole blocks.
      level.Data.resize((size_t) levelInfo.m_total_blocks * (size_t) blockBytes);

      if (!transcoder.transcode_image_level(levelIndex,
                                            0,
                                            0,
                                            level.Data.data(),
                                            levelInfo.m_total_blocks,
                                            target))
      {
        TK_WRN("KTX2: transcode failed for mip level %d.", (int) levelIndex);
        levels.clear();
        return false;
      }
    }

    return true;
  }

  bool Ktx2DecodeRgba8(const ubyte* data, size_t size, std::vector<TextureLevel>& levels)
  {
    levels.clear();

    if (!IsKtx2Data(data, size))
    {
      return false;
    }

    InitTranscoder();

    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(data, (uint32_t) size))
    {
      TK_WRN("KTX2: the container header could not be parsed.");
      return false;
    }

    if (transcoder.is_hdr())
    {
      TK_WRN("KTX2: HDR containers are not supported yet, keeping the texture unloaded.");
      return false;
    }

    if (!transcoder.start_transcoding())
    {
      TK_WRN("KTX2: start_transcoding failed, the container is unsupported or corrupt.");
      return false;
    }

    const uint32_t levelCount = transcoder.get_levels();
    levels.resize(levelCount);

    std::vector<ubyte> padded;

    for (uint32_t levelIndex = 0; levelIndex < levelCount; ++levelIndex)
    {
      basist::ktx2_image_level_info levelInfo;
      if (!transcoder.get_image_level_info(levelInfo, levelIndex, 0, 0))
      {
        TK_WRN("KTX2: no image info for mip level %d.", (int) levelIndex);
        levels.clear();
        return false;
      }

      const int srcWidth  = (int) levelInfo.m_orig_width;
      const int srcHeight = (int) levelInfo.m_orig_height;

      // An uncompressed transcode is written at the source dimensions, not at the padded block
      // grid: the transcoder's default row pitch for an uncompressed format is orig_width and its
      // row count is orig_height (basisu_transcoder.cpp, transcode_slice). Sizing the buffer from
      // the padded dimensions reads every row at the wrong stride.
      TextureLevel& level = levels[levelIndex];
      level.Width         = srcWidth;
      level.Height        = srcHeight;
      level.Data.resize((size_t) srcWidth * (size_t) srcHeight * 4u);

      if (!transcoder.transcode_image_level(levelIndex,
                                            0,
                                            0,
                                            level.Data.data(),
                                            (uint32_t) (srcWidth * srcHeight),
                                            basist::transcoder_texture_format::cTFRGBA32))
      {
        TK_WRN("KTX2: RGBA8 transcode failed for mip level %d.", (int) levelIndex);
        levels.clear();
        return false;
      }
    }

    return true;
  }

} // namespace ToolKit
