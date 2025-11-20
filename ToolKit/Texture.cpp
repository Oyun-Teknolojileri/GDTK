/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Texture.h"

#include "DirectionComponent.h"
#include "EngineSettings.h"
#include "FileManager.h"
#include "FullQuadPass.h"
#include "Image.h"
#include "Logger.h"
#include "Material.h"
#include "RHI.h"
#include "RenderSystem.h"
#include "Shader.h"
#include "Stats.h"
#include "TKOpenGL.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  // Texture
  //////////////////////////////////////////

  TKDefineClass(Texture, Resource);

  Texture::Texture()
  {
    m_settings  = {GraphicTypes::Target2D,
                   GraphicTypes::UVRepeat,
                   GraphicTypes::UVRepeat,
                   GraphicTypes::UVRepeat,
                   GraphicTypes::SampleLinearMipmapLinear,
                   GraphicTypes::SampleLinear,
                   GraphicTypes::FormatSRGB8_A8,
                   GraphicTypes::FormatRGBA,
                   GraphicTypes::TypeUnsignedByte,
                   1,
                   -1,
                   true};

    m_textureId = 0;
  }

  Texture::Texture(const String& file) : Texture() { SetFile(file); }

  void Texture::NativeConstruct(StringView label)
  {
    Super::NativeConstruct();
    m_label = label;
  }

  void Texture::NativeConstruct(int width, int height, const TextureSettings& settings, StringView label)
  {
    Super::NativeConstruct();

    m_width    = width;
    m_height   = height;
    m_settings = settings;
    m_label    = label;
  }

  Texture::~Texture()
  {
    UnInit();
    Clear();
  }

  void Texture::Load()
  {
    if (m_loaded)
    {
      return;
    }

    if (m_settings.Type == GraphicTypes::TypeFloat)
    {

      if ((m_imagef = GetFileManager()->GetHdriFile(GetFile(), &m_width, &m_height, &m_numChannels, 4)))
      {
        m_loaded = true;
      }
    }
    else
    {
      if ((m_image = GetFileManager()->GetImageFile(GetFile(), &m_width, &m_height, &m_numChannels, 4)))
      {
        m_loaded = true;
      }
    }
  }

  void Texture::Init(bool flushClientSideArray)
  {
    if (m_initiated)
    {
      return;
    }

    // Sanity checks
    if ((m_image == nullptr && m_imagef == nullptr))
    {
      assert(0 && "No texture data.");
      return;
    }

    if (m_width <= 0 || m_height <= 0)
    {
      assert(0 && "Zero texture size.");
      return;
    }

    assert(m_textureId == 0 && "Texture already initialized.");
    glGenTextures(1, &m_textureId);
    RHI::SetTexture((GLenum) m_settings.Target, m_textureId);

    uint64 pixelCount = (uint64) m_width * (uint64) m_height;
    if (m_settings.Type != GraphicTypes::TypeFloat)
    {
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   (GLint) m_settings.InternalFormat,
                   m_width,
                   m_height,
                   0,
                   (GLenum) m_settings.Format,
                   (GLenum) m_settings.Type,
                   m_image);
    }
    else
    {
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   (GLint) m_settings.InternalFormat,
                   m_width,
                   m_height,
                   0,
                   (GLenum) m_settings.Format,
                   (GLenum) m_settings.Type,
                   m_imagef);
    }

    ApplyTextureSettings(m_settings);
    Stats::AddVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat));

    if (TK_GL_EXT_texture_filter_anisotropic == 1)
    {
      float maxAniso = 1.0f;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);

      EngineSettings& settings = GetEngineSettings();
      int anisoVal             = settings.m_graphics->GetAnisotropicTextureFilteringVal().GetValue<int>();
      float aniso              = glm::max(1.0f, float(anisoVal));
      aniso                    = glm::min(maxAniso, aniso);

      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    }

    if (m_settings.GenerateMipMap)
    {
      glGenerateMipmap(GL_TEXTURE_2D);
    }

    if (flushClientSideArray)
    {
      Clear();
    }

    m_initiated = true;
  }

  void Texture::UnInit()
  {
    if (m_textureId == 0 || !m_initiated)
    {
      return;
    }

    uint64 pixelCount = (uint64) m_width * (uint64) m_height;
    if (m_settings.Target == GraphicTypes::Target2D)
    {
      if (m_settings.msaaCount > 1)
      {
        // There is no msaa render texture, so delete the renderbuffer.
        glDeleteRenderbuffers(1, &m_textureId);
        Stats::RemoveVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * m_settings.msaaCount);
      }
      else
      {
        RHI::DeleteTexture(m_textureId);
        Stats::RemoveVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat));
      }
    }
    else if (m_settings.Target == GraphicTypes::Target2DArray)
    {
      assert(m_settings.Layers > 0 && "Layer count must be greater than 0");
      RHI::DeleteTexture(m_textureId);
      Stats::RemoveVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * m_settings.Layers);
    }
    else if (m_settings.Target == GraphicTypes::TargetCubeMap)
    {
      RHI::DeleteTexture(m_textureId);
      Stats::RemoveVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * 6);
    }
    else
    {
      assert(false);
    }

    m_textureId = 0;
    m_initiated = false;
  }

  const TextureSettings& Texture::Settings() { return m_settings; }

  void Texture::Settings(const TextureSettings& settings) { m_settings = settings; }

  int Texture::CalculateMipmapLevels()
  {
    int maxDimension = glm::max(m_width, m_height);
    int mipLevels    = glm::log2(maxDimension) + 1;

    return mipLevels;
  }

  void Texture::GenerateMipMaps()
  {
    RHI::SetTexture((GLenum) m_settings.Target, m_textureId);
    glGenerateMipmap((GLenum) m_settings.Target);
  }

  bool Texture::IsMultiSampled() { return m_settings.msaaCount > 1; }

  void Texture::Resolve() { TK_WRN("Resolve not implemented for this texture type."); }

  void Texture::Clear()
  {
    ImageFree(m_image);
    ImageFree(m_imagef);

    m_image  = nullptr;
    m_imagef = nullptr;
  }

  void Texture::ApplyTextureSettings(const TextureSettings& settings)
  {
    glTexParameteri((GLenum) settings.Target, GL_TEXTURE_MIN_FILTER, (GLint) settings.MinFilter);
    glTexParameteri((GLenum) settings.Target, GL_TEXTURE_MAG_FILTER, (GLint) settings.MagFilter);
    glTexParameteri((GLenum) settings.Target, GL_TEXTURE_WRAP_S, (GLint) settings.WarpS);
    glTexParameteri((GLenum) settings.Target, GL_TEXTURE_WRAP_T, (GLint) settings.WarpT);

    if (settings.Target == GraphicTypes::TargetCubeMap)
    {
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, (GLint) settings.WarpR);
    }
  }

  // DepthTexture
  //////////////////////////////////////////

  TKDefineClass(DepthTexture, Texture);

  DepthTexture::DepthTexture()
  {
    m_settings.MinFilter = GraphicTypes::SampleNearest;
    m_settings.MagFilter = GraphicTypes::SampleNearest;
    m_settings.WarpS     = GraphicTypes::UVClampToEdge;
    m_settings.WarpT     = GraphicTypes::UVClampToEdge;
  }

  void DepthTexture::Load() {}

  void DepthTexture::Clear() { UnInit(); }

  int DepthTexture::GetFormatSize()
  {
    int internalFormatSize = m_stencil ? 4 : 3;

    int sizeMultiplier     = 1;
    if (m_settings.msaaCount > 1 && glRenderbufferStorageMultisampleEXT != nullptr)
    {
      sizeMultiplier = m_settings.msaaCount;
    }

    return internalFormatSize * sizeMultiplier;
  }

  void DepthTexture::Init(int width, int height, bool stencil, int multiSample)
  {
    if (m_initiated)
    {
      return;
    }

    m_initiated          = true;
    m_width              = width;
    m_height             = height;
    m_stencil            = stencil;
    m_settings.msaaCount = multiSample;

    if constexpr (GraphicSettings::disableMSAA)
    {
      m_settings.msaaCount = 1;
    }

    glGenRenderbuffers(1, &m_textureId);
    glBindRenderbuffer(GL_RENDERBUFFER, m_textureId);

    Stats::SetGpuResourceLabel(m_label, GpuResourceType::RenderBuffer, m_textureId);

    int sizeMultiplier = 1;
    if (m_settings.msaaCount > 1)
    {
      glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                       m_settings.msaaCount,
                                       (GLenum) GetDepthFormat(),
                                       m_width,
                                       m_height);

      sizeMultiplier = m_settings.msaaCount;
    }
    else
    {
      glRenderbufferStorage(GL_RENDERBUFFER, (GLenum) GetDepthFormat(), m_width, m_height);
    }

    Stats::AddVRAMUsageInBytes((uint64) (m_width * m_height) * GetFormatSize());
  }

  void DepthTexture::UnInit()
  {
    if (m_textureId == 0 || !m_initiated)
    {
      return;
    }

    glDeleteRenderbuffers(1, &m_textureId);
    Stats::RemoveVRAMUsageInBytes((uint64) (m_width * m_height) * GetFormatSize());

    m_textureId = 0;
    m_initiated = false;
    m_stencil   = false;
  }

  GraphicTypes DepthTexture::GetDepthFormat()
  {
    return m_stencil ? GraphicTypes::FormatDepth24Stencil8 : GraphicTypes::FormatDepth24;
  }

  void DepthTexture::Resolve()
  {
    // Create resolved depth texture if it doesn't exist
    uint resolvedTextureId = 0;
    if (m_resolvedTexture == nullptr)
    {
      glGenTextures(1, &resolvedTextureId);
      RHI::SetTexture(GL_TEXTURE_2D, resolvedTextureId);

      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   (GLenum) GetDepthFormat(),
                   m_width,
                   m_height,
                   0,
                   GL_DEPTH_COMPONENT,
                   GL_UNSIGNED_INT,
                   nullptr);

      // Apply basic texture settings for depth
      ApplyTextureSettings(m_settings);

      m_resolvedTexture                = MakeNewPtr<Texture>();
      m_resolvedTexture->m_textureId   = resolvedTextureId;
      m_resolvedTexture->m_width       = m_width;
      m_resolvedTexture->m_height      = m_height;

      TextureSettings resolvedSettings = m_settings;
      resolvedSettings.msaaCount       = 1;
      m_resolvedTexture->Settings(resolvedSettings);
      m_resolvedTexture->m_initiated = true;
    }
    else
    {
      resolvedTextureId = m_resolvedTexture->m_textureId;
    }

    // Create temporary framebuffers
    GLuint readFBO = 0, drawFBO = 0;
    glGenFramebuffers(1, &readFBO);
    glGenFramebuffers(1, &drawFBO);

    // Setup read framebuffer with MSAA depth renderbuffer
    RHI::SetFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    GLenum depthAttachment = m_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, depthAttachment, GL_RENDERBUFFER, m_textureId);

    // Setup draw framebuffer with resolved depth texture
    RHI::SetFramebuffer(GL_DRAW_FRAMEBUFFER, drawFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, depthAttachment, GL_TEXTURE_2D, resolvedTextureId, 0);

    // Check framebuffer completeness
    GLenum readStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    GLenum drawStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

    if (readStatus != GL_FRAMEBUFFER_COMPLETE || drawStatus != GL_FRAMEBUFFER_COMPLETE)
    {
      TK_ERR("Depth framebuffer incomplete during resolve. Read: 0x%X, Draw: 0x%X", readStatus, drawStatus);
      RHI::DeleteFramebuffers(1, &readFBO);
      RHI::DeleteFramebuffers(1, &drawFBO);
      return;
    }

    // Blit depth (and stencil if present)
    GLbitfield blitMask = m_stencil ? (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) : GL_DEPTH_BUFFER_BIT;

    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, blitMask, GL_NEAREST);

    // Cleanup
    glDeleteFramebuffers(1, &readFBO);
    glDeleteFramebuffers(1, &drawFBO);
  }

  // DataTexture
  //////////////////////////////////////////

  TKDefineClass(DataTexture, Texture);

  void DataTexture::Load() {}

  void DataTexture::Init(void* data)
  {
    if (m_initiated)
    {
      return;
    }

    assert(m_textureId == 0 && "Texture already initialized.");
    glGenTextures(1, &m_textureId);
    RHI::SetTexture((GLenum) m_settings.Target, m_textureId);

    glTexImage2D((GLenum) m_settings.Target,
                 0,
                 (GLint) m_settings.InternalFormat,
                 m_width,
                 m_height,
                 0,
                 (GLenum) m_settings.Format,
                 (GLenum) m_settings.Type,
                 data);

    ApplyTextureSettings(m_settings);

    m_loaded    = true;
    m_initiated = true;
  };

  void DataTexture::Map(void* data, uint64 size)
  {
    if (!m_initiated)
    {
      assert(false && "Texture must be initialized before mapping data.");
      return;
    }

    RHI::SetTexture((GLenum) m_settings.Target, m_textureId);

    glTexSubImage2D((GLenum) m_settings.Target,
                    0,
                    0,
                    0,
                    m_width,
                    m_height,
                    (GLenum) m_settings.Format,
                    (GLenum) m_settings.Type,
                    data);

    Stats::AddVRAMUsageInBytes(size);
  }

  void DataTexture::UnInit()
  {
    if (m_textureId == 0 || !m_initiated)
    {
      return;
    }

    RHI::DeleteTexture(m_textureId);
    Stats::RemoveVRAMUsageInBytes((uint64) (m_width * m_height) * BytesOfFormat(m_settings.InternalFormat));

    m_textureId = 0;
    m_loaded    = false;
    m_initiated = false;
  };

  // CubeMap
  //////////////////////////////////////////

  TKDefineClass(CubeMap, Texture);

  CubeMap::CubeMap() : Texture()
  {
    m_settings.Target    = GraphicTypes::TargetCubeMap;
    m_settings.MinFilter = GraphicTypes::SampleLinearMipmapLinear;
    m_settings.MagFilter = GraphicTypes::SampleLinear;
    m_settings.WarpR     = GraphicTypes::UVClampToEdge;
    m_settings.WarpS     = GraphicTypes::UVClampToEdge;
    m_settings.WarpT     = GraphicTypes::UVClampToEdge;
  }

  CubeMap::CubeMap(const String& file) : Texture() { SetFile(file); }

  CubeMap::~CubeMap() { UnInit(); }

  void CubeMap::Consume(RenderTargetPtr cubeMapTarget)
  {
    const TextureSettings& targetTextureSettings = cubeMapTarget->Settings();

    assert(targetTextureSettings.Target == GraphicTypes::TargetCubeMap);

    m_textureId  = cubeMapTarget->m_textureId;
    m_width      = cubeMapTarget->m_width;
    m_height     = cubeMapTarget->m_height;

    m_settings   = targetTextureSettings;
    m_initiated  = true;

    m_consumedRT = cubeMapTarget;
  }

  void CubeMap::Load()
  {
    if (m_loaded)
    {
      return;
    }

    m_images.resize(6);
    String fullPath = GetFile();
    size_t pos      = fullPath.find("px.png");
    if (pos == String::npos)
    {
      GetLogger()->Log("Inappropriate postfix. Looking for \"px.png\": " + fullPath);
      return;
    }

    String file = fullPath.substr(0, pos);
    for (int i = 0; i < 6; i++)
    {
      String postfix = "px.png";
      switch (i)
      {
      case 1:
        postfix = "nx.png";
        break;
      case 2:
        postfix = "py.png";
        break;
      case 3:
        postfix = "ny.png";
        break;
      case 4:
        postfix = "pz.png";
        break;
      case 5:
        postfix = "nz.png";
        break;
      }

      String name = file + postfix;
      if ((m_images[i] = GetFileManager()->GetImageFile(name, &m_width, &m_height, &m_numChannels, 0)))
      {
        GetLogger()->Log("Missing file: " + name);
        GetLogger()->Log("Cube map loading requires additional 5 png files with postfix "
                         "\"nx py ny pz nz\".");
        m_loaded = false;

        Clear();
        return;
      }
    }

    m_loaded = true;
  }

  void CubeMap::Init(bool flushClientSideArray)
  {
    if (m_initiated)
    {
      return;
    }

    if (!m_loaded)
    {
      return;
    }

    // Sanity check.
    if ((int) m_images.size() != 6 || m_width <= 0 || m_height <= 0)
    {
      return;
    }

    // This will be used when deleting the texture
    m_settings.InternalFormat = GraphicTypes::FormatRGBA;
    m_settings.Target         = GraphicTypes::TargetCubeMap;

    assert(m_textureId == 0 && "Texture already initialized.");
    glGenTextures(1, &m_textureId);
    RHI::SetTexture(GL_TEXTURE_CUBE_MAP, m_textureId);

    uint sides[6] = {GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                     GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                     GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                     GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                     GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                     GL_TEXTURE_CUBE_MAP_NEGATIVE_Z};

    for (int i = 0; i < 6; i++)
    {
      glTexImage2D(sides[i], 0, GL_RGBA, m_width, m_width, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_images[i]);
    }

    uint64 pixelCount = (uint64) m_width * (uint64) m_height;
    Stats::AddVRAMUsageInBytes(pixelCount * 4 * 6); // Component count * times face count.

    ApplyTextureSettings(m_settings);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    if (flushClientSideArray)
    {
      Clear();
    }

    m_initiated = true;
  }

  void CubeMap::UnInit()
  {
    Texture::UnInit();

    if (m_consumedRT)
    {
      m_consumedRT->m_initiated = false;
      m_consumedRT->m_textureId = 0;
      m_consumedRT              = nullptr;
    }

    Clear();
    m_initiated = false;
  }

  void CubeMap::AllocateMipMapStorage()
  {
    const int numMipLevels = CalculateMipmapLevels();

    // Pre-allocate storage for all mip levels
    for (int mip = 1; mip < numMipLevels; mip++)
    {
      int mipWidth  = m_width >> mip;
      int mipHeight = m_height >> mip;

      // Ensure minimum size of 1x1
      mipWidth      = glm::max(1, mipWidth);
      mipHeight     = glm::max(1, mipHeight);

      for (int face = 0; face < 6; face++)
      {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                     mip,
                     (GLint) m_settings.InternalFormat,
                     mipWidth,
                     mipHeight,
                     0,
                     (GLenum) m_settings.Format,
                     (GLenum) m_settings.Type,
                     nullptr // No data yet, just allocating
        );
      }
    }
  }

  void CubeMap::Clear()
  {
    for (int i = 0; i < m_images.size(); i++)
    {
      free(m_images[i]);
      m_images[i] = nullptr;
    }
    m_loaded = false;
  }

  // Hdri
  //////////////////////////////////////////

  TKDefineClass(Hdri, Texture);

  Hdri::Hdri()
  {
    m_settings.InternalFormat = GraphicTypes::FormatRGBA16F;
    m_settings.Type           = GraphicTypes::TypeFloat;
    m_settings.MinFilter      = GraphicTypes::SampleLinear;
    m_settings.GenerateMipMap = false;
  }

  Hdri::Hdri(const String& file) : Hdri() { SetFile(file); }

  Hdri::~Hdri() { UnInit(); }

  void Hdri::Load()
  {
    if (m_loaded)
    {
      return;
    }

    // Load hdri image
    Texture::Load();
  }

  void Hdri::Init(bool flushClientSideArray)
  {
    if (m_initiated || m_waitingForInit)
    {
      return;
    }

    // Sanity check.
    if (m_imagef == nullptr || m_width <= 0 || m_height <= 0)
    {
      return;
    }

    // Init 2D hdri texture
    Texture::Init(flushClientSideArray);
    m_initiated = false;

    if (_diffuseBakeFile.empty())
    {
      RenderTask task = {[this](Renderer* renderer) -> void
                         {
                           if (m_initiated)
                           {
                             m_waitingForInit = false;
                             return;
                           }

                           // Convert hdri image to cubemap images.
                           TexturePtr self = GetTextureManager()->Create<Texture>(GetFile());
                           uint size       = m_width / 4;
                           m_cubemap       = renderer->GenerateCubemapFrom2DTexture(self, size, 1.0f);

                           if (m_generateIrradianceCaches)
                           {
                             GenerateIrradianceCaches(renderer);
                           }

                           m_initiated      = true;
                           m_waitingForInit = false;
                         }};

      GetRenderSystem()->AddRenderTask(task);
    }
    else
    {
      RenderTask task = {[this](Renderer* renderer) -> void
                         {
                           if (m_initiated)
                           {
                             m_waitingForInit = false;
                             return;
                           }

                           LoadIrradianceCaches(renderer);

                           m_initiated      = true;
                           m_waitingForInit = false;

                           // Clear file path after initialization, otherwise init always loads from file.
                           // This isn't desired, only after load we should read from file. Settings changes should
                           // reflect during editor time or in game requests.
                           _diffuseBakeFile.clear();
                           _specularBakeFile.clear();
                         }};

      GetRenderSystem()->AddRenderTask(task);
    }

    m_waitingForInit = true;
  }

  void Hdri::UnInit()
  {
    if (m_initiated)
    {
      if (m_cubemap)
      {
        m_cubemap->UnInit();
      }

      if (m_diffuseEnvMap)
      {
        m_diffuseEnvMap->UnInit();
      }

      if (m_specularEnvMap)
      {
        m_specularEnvMap->UnInit();
      }
    }

    m_waitingForInit = false;
    Texture::UnInit();
  }

  void Hdri::LoadIrradianceCaches(Renderer* renderer)
  {
    // Convert hdri image to cubemap images.
    // Floating point texture settings for caches.
    TextureSettings fTexture;
    fTexture.InternalFormat = GraphicTypes::FormatRGBA16F;
    fTexture.Type           = GraphicTypes::TypeFloat;

    // Read diffuse irradiance cache map.
    String cacheFile        = _diffuseBakeFile + HDR;
    TexturePtr envCache     = MakeNewPtr<Texture>();
    envCache->Settings(fTexture);
    envCache->SetFile(cacheFile);
    envCache->Load();

    TextureManager* texMan = GetTextureManager();
    texMan->Manage(envCache);

    // One face of the cube map is 1/4 of the width.
    auto eq2Cube    = [](int width) -> int { return width / 4; };

    uint size       = eq2Cube(envCache->m_width);
    m_diffuseEnvMap = renderer->GenerateCubemapFrom2DTexture(envCache, size, 1.0f);

    // Read specular irradiance cache map. First image will be same as the hdri for specular IR cache.
    if (IsDynamic())
    {
      // This is not read from equirect image file.
      // It already represents size of a cubemap face.
      size = m_cubemap->m_width;
    }
    else
    {
      // If not constructed dynamically, load from disk
      TexturePtr self = texMan->Create<Texture>(GetFile());
      size            = eq2Cube(self->m_width);
      m_cubemap       = renderer->GenerateCubemapFrom2DTexture(self, size, 1.0f);
    }

    // Initial level '0' is just the copy of color map.
    TextureSettings srtSettings = m_cubemap->Settings();
    srtSettings.MinFilter       = GraphicTypes::SampleLinearMipmapLinear;
    srtSettings.GenerateMipMap  = false;

    RenderTargetPtr specRT      = MakeNewPtr<RenderTarget>(size, size, srtSettings, "SpecularIRCacheRT");
    specRT->Init();

    m_specularEnvMap = MakeNewPtr<CubeMap>();
    m_specularEnvMap->Consume(specRT);

    renderer->CopyCubeMapToMipLevel(m_cubemap, m_specularEnvMap, 0);

    // Try reading rest from disk.
    m_specularEnvMap->AllocateMipMapStorage();
    m_specularEnvMap->GenerateMipMaps();

    for (int i = 1; i < RHIConstants::SpecularIBLLods; i++)
    {
      String cacheFile = _specularBakeFile + std::to_string(i) + HDR;
      if (CheckFile(cacheFile))
      {
        TexturePtr texture = MakeNewPtr<Texture>();
        texture->Settings(fTexture);
        texture->SetFile(cacheFile);
        texture->Load();
        texMan->Manage(texture);

        CubeMapPtr specLodCube = renderer->GenerateCubemapFrom2DTexture(texture, eq2Cube(texture->m_width), 1.0f);
        renderer->CopyCubeMapToMipLevel(specLodCube, m_specularEnvMap, i);
      }
      else
      {
        TK_WRN("Missing specular irradiance cache LOD: %d Map: %s", i, _specularBakeFile.c_str());
      }
    }
  }

  void Hdri::GenerateIrradianceCaches(Renderer* renderer)
  {
    // Pre-filtered and mip mapped environment map
    m_specularEnvMap = renderer->GenerateSpecularEnvMap(m_cubemap, m_cubemap->m_width, RHIConstants::SpecularIBLLods);

    // Generate diffuse irradience cubemap images
    int size         = glm::max(64, m_width / 32); // Smaller size for diffuse.
    m_diffuseEnvMap  = renderer->GenerateDiffuseEnvMap(m_cubemap, size);
  }

  String Hdri::GenerateBakedEnvironmentFileBaseName()
  {
    String file = GetFile();
    String path, name, ext;
    if (file.empty())
    {
      // Produces a sky path from entity id. Used in procedural sky entities.
      path = TexturePath("sky_bake_");
      name = std::to_string(GetIdVal());
      ext  = HDR;
    }
    else
    {
      DecomposePath(file, &path, &name, &ext);
      path += GetPathSeparatorAsStr();
    }

    file = path + name;
    return GetRelativeResourcePath(file);
  }

  String CreateDefaultBakePath(const String& file, const String& postFix)
  {
    // Create a default path.
    int size        = sizeof("ToolKit");
    String fileName = file.substr(size);
    return ConcatPaths({"ToolKit", TKIrradianceCacheFolder, fileName + postFix});
  }

  String Hdri::ToDiffuseIrradianceFileName(const String& file)
  {
    if (HasToolKitRoot(file))
    {
      return CreateDefaultBakePath(file, "_diff_env_bake");
    }

    return ConcatPaths({TKIrradianceCacheFolder, file + "_diff_env_bake"});
  }

  String Hdri::ToSpecularIrradianceFileName(const String& file)
  {
    if (HasToolKitRoot(file))
    {
      return CreateDefaultBakePath(file, "_spec_env_bake_");
    }

    return ConcatPaths({TKIrradianceCacheFolder, file + "_spec_env_bake_"});
  }

  void Hdri::TrySettingCacheFiles(const String& baseName)
  {
    // Check sky hdr caches.
    if (!baseName.empty())
    {
      String bakeFile = TexturePath(ToDiffuseIrradianceFileName(baseName));
      if (CheckFile(bakeFile + HDR))
      {
        _diffuseBakeFile = bakeFile;
      }

      bakeFile = TexturePath(ToSpecularIrradianceFileName(baseName));
      if (CheckFile(bakeFile + "1" + HDR)) // Fist baked level is 1.
      {
        _specularBakeFile = bakeFile;
      }
    }
  }

  // RenderTarget
  //////////////////////////////////////////

  TKDefineClass(RenderTarget, Texture);

  RenderTarget::RenderTarget() { m_settings = {}; }

  RenderTarget::~RenderTarget() {}

  void RenderTarget::Load() {}

  void RenderTarget::Init(bool flushClientSideArray)
  {
    if (m_initiated)
    {
      return;
    }

    if (m_width <= 0 || m_height <= 0)
    {
      return;
    }

    // This will be used when deleting the texture
    m_settings.InternalFormat = m_settings.InternalFormat;
    m_settings.Target         = m_settings.Target;
    m_settings.Layers         = m_settings.Layers;

    // Create frame buffer color texture
    assert(m_textureId == 0 && "Texture already initialized.");

    if (m_settings.msaaCount > 1)
    {
      glGenRenderbuffers(1, &m_textureId);
      glBindRenderbuffer(GL_RENDERBUFFER, m_textureId);
    }
    else
    {
      glGenTextures(1, &m_textureId);
      RHI::SetTexture((GLenum) m_settings.Target, m_textureId);
    }

    Stats::SetGpuResourceLabel(m_label, GpuResourceType::Texture, m_textureId);

    uint64 pixelCount = (uint64) m_width * (uint64) m_height;
    if (m_settings.Target == GraphicTypes::Target2D)
    {
      if (m_settings.msaaCount > 1)
      {
        // Opengl 3.0 / es 3.0 does not support multisampled textures directly.
        // Render buffer is used.
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                         m_settings.msaaCount,
                                         (GLenum) m_settings.InternalFormat,
                                         m_width,
                                         m_height);
      }
      else
      {
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     (int) m_settings.InternalFormat,
                     m_width,
                     m_height,
                     0,
                     (int) m_settings.Format,
                     (int) m_settings.Type,
                     0);
      }

      ApplyTextureSettings(m_settings);
      Stats::AddVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * m_settings.msaaCount);
    }
    else if (m_settings.Target == GraphicTypes::TargetCubeMap)
    {
      for (uint i = 0; i < 6; i++)
      {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                     0,
                     (int) m_settings.InternalFormat,
                     m_width,
                     m_height,
                     0,
                     (int) m_settings.Format,
                     (int) m_settings.Type,
                     0);
      }

      ApplyTextureSettings(m_settings);
      Stats::AddVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * 6);
    }
    else if (m_settings.Target == GraphicTypes::Target2DArray)
    {
      assert(m_settings.Layers > 0 && "Layer count must be at least 1");
      glTexImage3D(GL_TEXTURE_2D_ARRAY,
                   0,
                   (int) m_settings.InternalFormat,
                   m_width,
                   m_height,
                   m_settings.Layers,
                   0,
                   (int) m_settings.Format,
                   (int) m_settings.Type,
                   nullptr);

      ApplyTextureSettings(m_settings);
      Stats::AddVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * m_settings.Layers);
    }

    m_initiated = true;
  }

  void RenderTarget::Reconstruct(int width, int height, const TextureSettings& settings)
  {
    UnInit();

    m_width    = width;
    m_height   = height;
    m_settings = settings;

    Init();
  }

  void RenderTarget::ReconstructIfNeeded(int width, int height, const TextureSettings* settings)
  {
    bool reconstruct  = settings != nullptr ? *settings != m_settings : false;
    reconstruct      |= !m_initiated || m_width != width || m_height != height;

    if (reconstruct)
    {
      Reconstruct(width, height, settings != nullptr ? *settings : m_settings);
    }
  }

  void RenderTarget::Resolve()
  {
    assert(m_initiated && "Render target is not initialized.");
    assert(IsMultiSampled() && "Render target is not multi sampled.");

    // MSAA render target is a renderbuffer, we need to create a regular texture
    uint resolvedTextureId = 0;
    if (m_resolvedTexture == nullptr)
    {
      // Create single-sample texture for resolved output
      glGenTextures(1, &resolvedTextureId);
      RHI::SetTexture(GL_TEXTURE_2D, resolvedTextureId);

      // Allocate storage for resolved texture
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   (int) m_settings.InternalFormat,
                   m_width,
                   m_height,
                   0,
                   (int) m_settings.Format,
                   (int) m_settings.Type,
                   nullptr);

      // Apply texture settings to resolved texture
      TextureSettings resolvedSettings = m_settings;
      resolvedSettings.msaaCount       = 1;
      ApplyTextureSettings(resolvedSettings);

      // Generate mipmaps if needed
      if (m_settings.GenerateMipMap)
      {
        glGenerateMipmap(GL_TEXTURE_2D);
      }

      m_resolvedTexture              = MakeNewPtr<RenderTarget>();
      m_resolvedTexture->m_textureId = resolvedTextureId;
      m_resolvedTexture->m_width     = m_width;
      m_resolvedTexture->m_height    = m_height;
      m_resolvedTexture->Settings(resolvedSettings);
      m_resolvedTexture->m_initiated = true;
    }
    else
    {
      resolvedTextureId = m_resolvedTexture->m_textureId;
    }

    // We need two framebuffers: one for MSAA renderbuffer (read), one for resolved texture (draw)
    GLuint readFBO = 0, drawFBO = 0;

    glGenFramebuffers(1, &readFBO);
    glGenFramebuffers(1, &drawFBO);

    // Attach MSAA renderbuffer to read framebuffer
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_textureId);

    // Attach resolved texture to draw framebuffer
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolvedTextureId, 0);

    // Check framebuffer completeness
    GLenum readStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    GLenum drawStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

    if (readStatus != GL_FRAMEBUFFER_COMPLETE || drawStatus != GL_FRAMEBUFFER_COMPLETE)
    {
      TK_ERR("Framebuffer incomplete during MSAA resolve. Read: 0x%X, Draw: 0x%X", readStatus, drawStatus);
      glDeleteFramebuffers(1, &readFBO);
      glDeleteFramebuffers(1, &drawFBO);
      return;
    }

    // Blit from MSAA to single-sample (this performs the resolve)
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // Cleanup temporary framebuffers
    glDeleteFramebuffers(1, &readFBO);
    glDeleteFramebuffers(1, &drawFBO);
  }

  // TextureManager
  //////////////////////////////////////////

  TextureManager::TextureManager() { m_baseType = Texture::StaticClass(); }

  TextureManager::~TextureManager() {}

  bool TextureManager::CanStore(ClassMeta* Class)
  {
    if (Class->IsSublcassOf(Texture::StaticClass()))
    {
      return true;
    }

    return false;
  }

  String TextureManager::GetDefaultResource(ClassMeta* Class)
  {
    if (Class == Hdri::StaticClass())
    {
      return TexturePath(TKDefaultHdri + HDR, true);
    }
    else
    {
      return TexturePath(TKDefaultImage, true);
    }
  }
} // namespace ToolKit
