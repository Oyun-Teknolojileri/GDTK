/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "TextureCompression.h"

#include "Image.h"
#include "Logger.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>

#include "basisu_comp.h"

namespace ToolKit
{

  namespace
  {
    void InitEncoder()
    {
      // basisu_encoder_init() sets up the SIMD paths and the transcoder the encoder validates
      // against; it is not reentrant.
      static std::once_flag once;
      std::call_once(once,
                     []()
                     {
                       basisu::basisu_encoder_init();
                     });
    }

    basist::basis_tex_format ToBasisFormat(TextureCompressionOptions::Codec codec)
    {
      return codec == TextureCompressionOptions::Codec::UASTC ? basist::basis_tex_format::cUASTC_LDR_4x4
                                                              : basist::basis_tex_format::cETC1S;
    }

    bool WriteFileBytes(const String& path, const std::vector<ubyte>& bytes)
    {
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      if (!file.is_open())
      {
        return false;
      }

      file.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize) bytes.size());
      return file.good();
    }

    /**
     * Puts an image in the row order the GL upload expects.
     *
     * glTexImage2D treats the first row it is handed as the bottom row, and the engine has always
     * satisfied that by loading images with stb's vertical flip on (RenderSystem sets it when a GL
     * context comes up). The encoder has to hand over the same orientation, so the flip happens
     * here explicitly instead of depending on whether the host initialized a renderer: Import and
     * Packer both run headless, where that switch is still off.
     */
    void FlipRows(std::vector<ubyte>& rgba, int width, int height)
    {
      const size_t stride = (size_t) width * 4u;
      std::vector<ubyte> row(stride);
      for (int y = 0; y < height / 2; ++y)
      {
        ubyte* top    = &rgba[(size_t) y * stride];
        ubyte* bottom = &rgba[(size_t) (height - 1 - y) * stride];

        std::memcpy(row.data(), top, stride);
        std::memcpy(top, bottom, stride);
        std::memcpy(bottom, row.data(), stride);
      }
    }

    /** Decodes an image file bottom up, ready for the encoder. */
    bool LoadImageForEncoding(const String& path, std::vector<ubyte>& rgba, int& width, int& height)
    {
      int channels = 0;
      ubyte* pixels = ImageLoadTopDown(path, &width, &height, &channels, 4);
      if (pixels == nullptr)
      {
        return false;
      }

      rgba.assign(pixels, pixels + (size_t) width * (size_t) height * 4u);
      ImageFree(pixels);

      FlipRows(rgba, width, height);
      return true;
    }

    bool Encode(const std::vector<ubyte>& rgba,
                int width,
                int height,
                const TextureCompressionOptions& options,
                std::vector<ubyte>& outKtx2)
    {
      outKtx2.clear();

      if (width <= 0 || height <= 0 || rgba.size() < (size_t) width * (size_t) height * 4u)
      {
        TK_ERR("Texture compression: invalid source image (%dx%d, %zu bytes).", width, height, rgba.size());
        return false;
      }

      if (width > (int) basisu::BASISU_MAX_IMAGE_DIMENSION || height > (int) basisu::BASISU_MAX_IMAGE_DIMENSION)
      {
        TK_ERR("Texture compression: %dx%d exceeds the encoder limit of %d.",
               width,
               height,
               (int) basisu::BASISU_MAX_IMAGE_DIMENSION);
        return false;
      }

      InitEncoder();

      basisu::basis_compressor_params params;
      params.set_format_mode_and_quality_effort(ToBasisFormat(options.Format), options.Quality, options.Effort);

      // sRGB drives the error metric, the mip filter and the transfer function written to the DFD.
      // The runtime ignores the DFD when it picks the GL internal format (the material decides that
      // per use), but a correct file is what an external tool reads back.
      params.set_srgb_options(options.Srgb);
      params.set_ldr_srgb_channel_weights(options.Srgb);

      params.m_check_for_alpha                   = true;
      params.m_mip_gen                           = options.GenerateMipmaps;
      params.m_mip_renormalize                   = options.NormalMap;
      params.m_tex_type                          = basist::basis_texture_type::cBASISTexType2D;
      params.m_create_ktx2_file                  = true;
      params.m_write_output_basis_or_ktx2_files  = false;
      params.m_ktx2_uastc_supercompression       = basist::KTX2_SS_NONE;
      params.m_multithreading                    = false;
      params.m_debug                             = false;
      params.m_status_output                     = false;
      params.m_compute_stats                     = false;

      params.m_source_images.push_back(basisu::image(rgba.data(), (uint32_t) width, (uint32_t) height, 4));

      // A job pool is mandatory even with threading off: 1 means the calling thread alone.
      basisu::job_pool pool(1);
      params.m_pJob_pool = &pool;

      basisu::basis_compressor compressor;
      if (!compressor.init(params))
      {
        TK_ERR("Texture compression: encoder init failed for a %dx%d image.", width, height);
        return false;
      }

      const basisu::basis_compressor::error_code result = compressor.process();
      if (result != basisu::basis_compressor::cECSuccess)
      {
        TK_ERR("Texture compression: encode failed for a %dx%d image (error %d).", width, height, (int) result);
        return false;
      }

      const basisu::uint8_vec& ktx2 = compressor.get_output_ktx2_file();
      if (ktx2.empty())
      {
        TK_ERR("Texture compression: the encoder produced no KTX2 data for a %dx%d image.", width, height);
        return false;
      }

      outKtx2.assign(ktx2.begin(), ktx2.end());
      return true;
    }
  } // namespace

  bool CompressImageToKtx2(const ubyte* rgba8,
                           int width,
                           int height,
                           const TextureCompressionOptions& options,
                           std::vector<ubyte>& outKtx2)
  {
    outKtx2.clear();
    if (rgba8 == nullptr || width <= 0 || height <= 0)
    {
      return false;
    }

    const size_t byteCount = (size_t) width * (size_t) height * 4u;
    const std::vector<ubyte> pixels(rgba8, rgba8 + byteCount);
    return Encode(pixels, width, height, options, outKtx2);
  }

  bool CompressFileToMemory(const String& sourceImage,
                            const TextureCompressionOptions& options,
                            std::vector<ubyte>& outKtx2)
  {
    outKtx2.clear();

    std::vector<ubyte> rgba;
    int width  = 0;
    int height = 0;
    if (!LoadImageForEncoding(sourceImage, rgba, width, height))
    {
      TK_ERR("Texture compression: could not decode '%s'.", sourceImage.c_str());
      return false;
    }

    if (!Encode(rgba, width, height, options, outKtx2))
    {
      return false;
    }

    TK_LOG("Texture compression: '%s' %dx%d -> %zu bytes (%s).",
           sourceImage.c_str(),
           width,
           height,
           outKtx2.size(),
           options.Format == TextureCompressionOptions::Codec::UASTC ? "UASTC" : "ETC1S");
    return true;
  }

  bool CompressFileToKtx2(const String& sourceImage,
                          const String& destKtx2,
                          const TextureCompressionOptions& options)
  {
    std::vector<ubyte> bytes;
    if (!CompressFileToMemory(sourceImage, options, bytes))
    {
      return false;
    }

    if (!WriteFileBytes(destKtx2, bytes))
    {
      TK_ERR("Texture compression: could not write '%s'.", destKtx2.c_str());
      return false;
    }

    return true;
  }

  bool CompressFiles(const std::vector<TextureCompressionJob>& jobs, int threadCount)
  {
    if (jobs.empty())
    {
      return true;
    }

    // One texture per thread, and single threaded inside one texture: the bytes a texture encodes to
    // do not depend on threadCount, which matters because the same source is encoded by Import and
    // later by the Packer.
    const int workers = std::max(1, std::min(threadCount, (int) jobs.size()));

    std::atomic<size_t> next {0};
    std::atomic<int> failed {0};

    auto worker = [&jobs, &next, &failed]()
    {
      for (;;)
      {
        const size_t index = next.fetch_add(1);
        if (index >= jobs.size())
        {
          return;
        }

        const TextureCompressionJob& job = jobs[index];
        if (!CompressFileToKtx2(job.Source, job.Destination, job.Options))
        {
          failed.fetch_add(1);
        }
      }
    };

    if (workers == 1)
    {
      worker();
    }
    else
    {
      std::vector<std::thread> threads;
      threads.reserve((size_t) workers);
      for (int i = 0; i < workers; ++i)
      {
        threads.emplace_back(worker);
      }
      for (std::thread& thread : threads)
      {
        thread.join();
      }
    }

    return failed.load() == 0;
  }

} // namespace ToolKit

