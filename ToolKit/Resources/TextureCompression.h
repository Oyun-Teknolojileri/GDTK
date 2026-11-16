/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file TextureCompression.h
 * Encoding textures to KTX2 (Basis Universal).
 *
 * This is the authoring half of the texture compression pipeline: the Import tool writes `.ktx2`
 * containers with it, and the pack step converts the images a project still carries in an
 * uncompressed format. The runtime half is `Ktx2Texture.h`, which reads what this file writes.
 *
 * The encoder is a host feature. Web and Android builds link a stub that logs once and returns
 * false, so a game payload never carries it (`TextureCompressionStub.cpp`).
 *
 * Encoding is deterministic: every entry point runs basisu single threaded, so the same input image
 * and options always produce the same bytes. `CompressFiles` parallelises across textures, never
 * inside one, which keeps that property.
 */

#include "Types.h"

namespace ToolKit
{

  struct TextureCompressionOptions
  {
    enum class Codec
    {
      /** Small, perceptual, for albedo and emissive maps. Roughly 0.3-1.5 bits per texel. */
      ETC1S,
      /** High quality, ~8 bits per texel before the KTX2 container is compressed. Data maps. */
      UASTC
    };

    /** Named Format, not Codec: a member with the same name as its type hides the type's name
     *  inside the struct, and `TextureCompressionOptions::Codec::UASTC` then does not compile. */
    Codec Format        = Codec::ETC1S;
    /** 1-100. Higher keeps more detail and produces a larger file. */
    int Quality         = 75;
    /** 0-10. Higher searches harder for a smaller file and compresses slower. */
    int Effort          = 3;
    /** sRGB metrics and sRGB mip filtering. False for normal, metallic-roughness and mask maps. */
    bool Srgb           = true;
    /** Linear metrics with flat channel weights and normal map mip renormalisation. */
    bool NormalMap      = false;
    /** Mip chain baked into the container. The runtime never generates one for these. */
    bool GenerateMipmaps = true;
  };

  /** One texture for CompressFiles. Destination is a path; the file is written by the call. */
  struct TextureCompressionJob
  {
    String Source;
    String Destination;
    TextureCompressionOptions Options;
  };

  /**
   * Encodes 8 bit RGBA pixels into a KTX2 container.
   *
   * The pixels are taken exactly as given: use `CompressFileToKtx2` / `CompressFileToMemory` when the
   * source is an image file, because those also flip the rows into the order the GL upload expects.
   *
   * @param rgba8 Source pixels, tightly packed, 4 bytes per texel.
   * @param width Image width in texels.
   * @param height Image height in texels.
   * @param options Codec and quality settings.
   * @param outKtx2 Receives the container. Empty on failure.
   * @return False when the encoder rejects the input or runs out of memory.
   */
  TK_API bool CompressImageToKtx2(const ubyte* rgba8,
                                  int width,
                                  int height,
                                  const TextureCompressionOptions& options,
                                  std::vector<ubyte>& outKtx2);

  /**
   * Decodes an image file and encodes it to a KTX2 container, returning the bytes.
   *
   * @param sourceImage PNG / JPG / TGA / BMP / PSD path.
   * @param options Codec and quality settings.
   * @param outKtx2 Receives the container. Empty on failure.
   * @return False when the image cannot be decoded or encoded.
   */
  TK_API bool CompressFileToMemory(const String& sourceImage,
                                   const TextureCompressionOptions& options,
                                   std::vector<ubyte>& outKtx2);

  /**
   * Same as CompressFileToMemory, and writes the result to destKtx2.
   * @param sourceImage Image file to read.
   * @param destKtx2 Container path to write.
   * @param options Codec and quality settings.
   * @return False when the image cannot be decoded, encoded or written.
   */
  TK_API bool CompressFileToKtx2(const String& sourceImage,
                                 const String& destKtx2,
                                 const TextureCompressionOptions& options);

  /**
   * Encodes several textures, one thread per texture.
   *
   * Each encode is internally single threaded, so the bytes do not depend on threadCount.
   *
   * @param jobs Texture / destination pairs. A failed job leaves its destination unwritten and is
   * reported, the rest continue.
   * @param threadCount Number of textures to encode at once. 0 or 1 runs them all on this thread.
   * @return True when every job succeeded.
   */
  TK_API bool CompressFiles(const std::vector<TextureCompressionJob>& jobs, int threadCount);

} // namespace ToolKit
