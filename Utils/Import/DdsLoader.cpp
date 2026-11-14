/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include <DdsLoader.h>

#include <Image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace ToolKit
{

  namespace
  {
    constexpr uint DdsMagic           = 0x20534444; // "DDS " read as a little endian word.
    constexpr uint DdsHeaderSize      = 124;
    constexpr uint DdsPixelFormatFcc  = 0x4; // DDPF_FOURCC, the pixel data is block compressed.
    constexpr size_t DdsMagicSize       = 4;
    constexpr size_t Dx10HeaderSize     = 20;

    // DXGI_FORMAT values of the BC formats, used by files with a "DX10" extended header.
    constexpr uint DxgiBc1Unorm     = 71;
    constexpr uint DxgiBc1UnormSrgb = 72;
    constexpr uint DxgiBc3Unorm     = 77;
    constexpr uint DxgiBc3UnormSrgb = 78;
    constexpr uint DxgiBc5Unorm     = 83;

    constexpr uint MakeFourCC(char a, char b, char c, char d)
    {
      return uint(a) | (uint(b) << 8) | (uint(c) << 16) | (uint(d) << 24);
    }

#pragma pack(push, 1)
    struct DdsPixelFormat
    {
      uint size;
      uint flags;
      uint fourCC;
      uint rgbBitCount;
      uint rMask;
      uint gMask;
      uint bMask;
      uint aMask;
    };

    struct DdsHeader
    {
      uint size;
      uint flags;
      uint height;
      uint width;
      uint pitchOrLinearSize;
      uint depth;
      uint mipMapCount;
      uint reserved[11];
      DdsPixelFormat pixelFormat;
      uint caps;
      uint caps2;
      uint caps3;
      uint caps4;
      uint reserved2;
    };
#pragma pack(pop)

    static_assert(sizeof(DdsHeader) == DdsHeaderSize, "DDS header must be 124 bytes.");

    enum class DdsFormat
    {
      Unsupported,
      Bc1,
      Bc3,
      Bc5
    };

    // Block compressed layouts: 4x4 texels per block, BC1 stores 8 bytes per block and BC3 / BC5
    // store 16.
    constexpr uint BlockEdge        = 4;
    constexpr uint BlockTexels      = BlockEdge * BlockEdge;
    constexpr uint Channels         = 4;
    constexpr size_t Bc1BlockBytes  = 8;
    constexpr size_t Bc23BlockBytes = 16;

    /**
     * Expands a 5-6-5 color into 8 bits per channel. The alpha byte is left opaque; a caller that
     * needs the three color mode overwrites it.
     */
    void UnpackRgb565(uint16 color, ubyte* rgba)
    {
      const uint r = (color >> 11) & 0x1F;
      const uint g = (color >> 5) & 0x3F;
      const uint b = color & 0x1F;

      rgba[0] = (ubyte) ((r * 255) / 31);
      rgba[1] = (ubyte) ((g * 255) / 63);
      rgba[2] = (ubyte) ((b * 255) / 31);
      rgba[3] = 255;
    }

    /**
     * Decodes one 8 byte BC1 block into 16 RGBA texels. A block whose color0 is not greater than
     * its color1 is the three color mode with a transparent texel, which is how DXT1 carries its
     * one bit alpha for cutouts. BC3 reuses the color half of a block but always runs in the four
     * color mode, which is what threeColorMode carries.
     */
    void DecodeBc1Block(const ubyte* src, ubyte* texels, bool threeColorMode)
    {
      const uint16 color0 = uint16(src[0]) | (uint16(src[1]) << 8);
      const uint16 color1 = uint16(src[2]) | (uint16(src[3]) << 8);

      ubyte palette[4][4];
      UnpackRgb565(color0, palette[0]);
      UnpackRgb565(color1, palette[1]);

      if (color0 > color1 || !threeColorMode)
      {
        for (int i = 0; i < 3; ++i)
        {
          palette[2][i] = (ubyte) ((2 * palette[0][i] + palette[1][i]) / 3);
          palette[3][i] = (ubyte) ((palette[0][i] + 2 * palette[1][i]) / 3);
        }
        palette[2][3] = 255;
        palette[3][3] = 255;
      }
      else
      {
        for (int i = 0; i < 3; ++i)
        {
          palette[2][i] = (ubyte) ((palette[0][i] + palette[1][i]) / 2);
          palette[3][i] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
      }

      // One index byte per row of the 4x4 block, two bits per texel, leftmost texel in the low bits.
      for (int texel = 0; texel < 16; ++texel)
      {
        const uint bits = (uint(src[4 + texel / 4]) >> (2 * (texel % 4))) & 0x3;
        memcpy(&texels[texel * 4], palette[bits], 4);
      }
    }

    /** Decodes one 8 byte BC4 block, i.e. 16 single channel values, into values. */
    void DecodeBc4Block(const ubyte* src, ubyte* values)
    {
      const ubyte value0 = src[0];
      const ubyte value1 = src[1];

      ubyte palette[8];
      palette[0] = value0;
      palette[1] = value1;

      if (value0 > value1)
      {
        for (int i = 1; i <= 6; ++i)
        {
          palette[i + 1] = (ubyte) (((7 - i) * value0 + i * value1) / 7);
        }
      }
      else
      {
        for (int i = 1; i <= 4; ++i)
        {
          palette[i + 1] = (ubyte) (((5 - i) * value0 + i * value1) / 5);
        }
        palette[6] = 0;
        palette[7] = 255;
      }

      uint64 bits = 0;
      for (int i = 0; i < 6; ++i)
      {
        bits |= uint64(src[2 + i]) << (8 * i);
      }

      for (int texel = 0; texel < 16; ++texel)
      {
        values[texel] = palette[(bits >> (3 * texel)) & 0x7];
      }
    }

    /** Decodes one 16 byte BC3 block: the BC4 alpha half followed by the BC1 color half. */
    void DecodeBc3Block(const ubyte* src, ubyte* texels)
    {
      ubyte alpha[16];
      DecodeBc4Block(src, alpha);
      DecodeBc1Block(src + 8, texels, false);

      for (int texel = 0; texel < 16; ++texel)
      {
        texels[texel * 4 + 3] = alpha[texel];
      }
    }

    /**
     * Decodes one 16 byte BC5 block. The format stores two BC4 channels (the x and y of an encoded
     * normal); z is rebuilt from them and written to the blue channel, because the engine reads all
     * three components of a normal map (defaultFragment.shader: n * 2.0 - 1.0).
     */
    void DecodeBc5Block(const ubyte* src, ubyte* texels)
    {
      ubyte red[16];
      ubyte green[16];
      DecodeBc4Block(src, red);
      DecodeBc4Block(src + 8, green);

      for (int texel = 0; texel < 16; ++texel)
      {
        const float x = (red[texel] / 255.0f) * 2.0f - 1.0f;
        const float y = (green[texel] / 255.0f) * 2.0f - 1.0f;
        const float z = std::sqrt(std::max(0.0f, 1.0f - (x * x) - (y * y)));

        texels[texel * 4 + 0] = red[texel];
        texels[texel * 4 + 1] = green[texel];
        texels[texel * 4 + 2] = (ubyte) ((z * 0.5f + 0.5f) * 255.0f + 0.5f);
        texels[texel * 4 + 3] = 255;
      }
    }

    DdsFormat ResolveFormat(uint fourCC, uint dxgiFormat, bool hasDx10Header)
    {
      if (hasDx10Header)
      {
        switch (dxgiFormat)
        {
          case DxgiBc1Unorm:
          case DxgiBc1UnormSrgb:
            return DdsFormat::Bc1;
          case DxgiBc3Unorm:
          case DxgiBc3UnormSrgb:
            return DdsFormat::Bc3;
          case DxgiBc5Unorm:
            return DdsFormat::Bc5;
          default:
            return DdsFormat::Unsupported;
        }
      }

      if (fourCC == MakeFourCC('D', 'X', 'T', '1'))
      {
        return DdsFormat::Bc1;
      }
      if (fourCC == MakeFourCC('D', 'X', 'T', '5'))
      {
        return DdsFormat::Bc3;
      }
      if (fourCC == MakeFourCC('A', 'T', 'I', '2') || fourCC == MakeFourCC('B', 'C', '5', 'U'))
      {
        return DdsFormat::Bc5;
      }

      return DdsFormat::Unsupported;
    }

  } // namespace

  bool ConvertDdsToPng(const String& ddsFile, const String& pngFile)
  {
    std::ifstream file(ddsFile.c_str(), std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
      return false;
    }

    const std::streamoff fileSize = file.tellg();
    if (fileSize < (std::streamoff) (DdsMagicSize + DdsHeaderSize))
    {
      return false;
    }

    std::vector<ubyte> buffer((size_t) fileSize);
    file.seekg(0, std::ios::beg);
    file.read((char*) buffer.data(), (std::streamsize) fileSize);
    const bool read = file.gcount() == (std::streamsize) fileSize;
    file.close();

    if (!read)
    {
      return false;
    }

    uint magic = 0;
    memcpy(&magic, buffer.data(), sizeof(magic));
    if (magic != DdsMagic)
    {
      return false;
    }

    DdsHeader header;
    memcpy(&header, buffer.data() + DdsMagicSize, sizeof(header));

    if (header.size != DdsHeaderSize || header.width == 0 || header.height == 0)
    {
      return false;
    }

    if ((header.pixelFormat.flags & DdsPixelFormatFcc) == 0)
    {
      return false;
    }

    size_t dataOffset  = DdsMagicSize + DdsHeaderSize;
    const uint fourCC  = header.pixelFormat.fourCC;
    const bool hasDx10 = fourCC == MakeFourCC('D', 'X', '1', '0');
    uint dxgiFormat    = 0;

    if (hasDx10)
    {
      if (buffer.size() < dataOffset + Dx10HeaderSize)
      {
        return false;
      }

      memcpy(&dxgiFormat, buffer.data() + dataOffset, sizeof(dxgiFormat));
      dataOffset += Dx10HeaderSize;
    }

    const DdsFormat format = ResolveFormat(fourCC, dxgiFormat, hasDx10);
    if (format == DdsFormat::Unsupported)
    {
      return false;
    }

    const uint blocksWide   = (header.width + BlockEdge - 1) / BlockEdge;
    const uint blocksHigh   = (header.height + BlockEdge - 1) / BlockEdge;
    const size_t blockBytes = format == DdsFormat::Bc1 ? Bc1BlockBytes : Bc23BlockBytes;
    const size_t blockCount = (size_t) blocksWide * (size_t) blocksHigh;
    const size_t dataSize   = blockCount * blockBytes;

    if (dataOffset + dataSize > buffer.size())
    {
      return false;
    }

    std::vector<ubyte> pixels((size_t) header.width * (size_t) header.height * Channels);
    ubyte texels[BlockTexels * Channels];

    for (uint blockY = 0; blockY < blocksHigh; ++blockY)
    {
      for (uint blockX = 0; blockX < blocksWide; ++blockX)
      {
        const ubyte* src = buffer.data() + dataOffset + ((size_t) blockY * blocksWide + blockX) * blockBytes;

        switch (format)
        {
          case DdsFormat::Bc1:
            DecodeBc1Block(src, texels, true);
            break;
          case DdsFormat::Bc3:
            DecodeBc3Block(src, texels);
            break;
          case DdsFormat::Bc5:
            DecodeBc5Block(src, texels);
            break;
          default:
            return false;
        }

        for (uint y = 0; y < BlockEdge; ++y)
        {
          const uint dstY = blockY * BlockEdge + y;
          if (dstY >= header.height)
          {
            break;
          }

          for (uint x = 0; x < BlockEdge; ++x)
          {
            const uint dstX = blockX * BlockEdge + x;
            if (dstX >= header.width)
            {
              break;
            }

            const size_t dst = ((size_t) dstY * header.width + dstX) * Channels;
            memcpy(&pixels[dst], &texels[(y * BlockEdge + x) * Channels], Channels);
          }
        }
      }
    }

    return WritePNG(pngFile.c_str(), (int) header.width, (int) header.height, Channels, pixels.data(), 0) != 0;
  }

} // namespace ToolKit
