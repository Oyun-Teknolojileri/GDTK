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
#include "IGraphicsBackend.h"
#include "Image.h"
#include "Logger.h"
#include "Material.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "Shader.h"
#include "Stats.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  // Texture
  //////////////////////////////////////////

  TKDefineClass(Texture, Resource);

  Texture::Texture()
  {
    m_settings = {GraphicTypes::Target2D,
                  GraphicTypes::UVRepeat,
                  GraphicTypes::UVRepeat,
                  GraphicTypes::UVRepeat,
                  GraphicTypes::SampleLinearMipmapLinear,
                  GraphicTypes::SampleLinear,
                  GraphicTypes::FormatSRGB8_A8,
                  GraphicTypes::FormatRGBA,
                  GraphicTypes::TypeUnsignedByte,
                  MsaaSampleCount::x0,
                  -1,
                  true};
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

    assert(m_gpuData == nullptr && "Texture already initialized.");

    uint64 pixelCount         = (uint64) m_width * (uint64) m_height;
    IGraphicsBackend* backend = GetRenderSystem()->GetBackend();
    assert(backend && "Graphics backend not available during Texture::Init");

    backend->CreateTexture(this);
    backend->ApplyTextureSettings(this);
    Stats::AddVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat));

    if (m_settings.GenerateMipMap)
    {
      backend->GenerateMipmaps(this);
    }

    if (flushClientSideArray)
    {
      Clear();
    }

    m_initiated = true;
  }

  void Texture::UnInit()
  {
    if (m_gpuData == nullptr || !m_initiated)
    {
      return;
    }

    uint64 pixelCount = (uint64) m_width * (uint64) m_height;
    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->DestroyTexture(this);
    }

    // VRAM accounting
    if (m_settings.Target == GraphicTypes::Target2D)
    {
      int msaaScale = m_settings.msaaCount > MsaaSampleCount::x0 ? (int) m_settings.msaaCount : 1;
      Stats::RemoveVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * msaaScale);
    }
    else if (m_settings.Target == GraphicTypes::Target2DArray)
    {
      assert(m_settings.Layers > 0 && "Layer count must be greater than 0");
      Stats::RemoveVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * m_settings.Layers);
    }
    else if (m_settings.Target == GraphicTypes::TargetCubeMap)
    {
      Stats::RemoveVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * 6);
    }

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
    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->GenerateMipmaps(this);
    }
  }

  bool Texture::IsMultiSampled() { return m_settings.msaaCount > MsaaSampleCount::x0; }

  TexturePtr Texture::GetResolvedTexture()
  {
    return m_resolvedTexture != nullptr && IsMultiSampled() ? m_resolvedTexture : nullptr;
  }

  void Texture::Clear()
  {
    ImageFree(m_image);
    ImageFree(m_imagef);

    m_image  = nullptr;
    m_imagef = nullptr;
  }

  void Texture::ApplyTextureSettings(const TextureSettings& settings)
  {
    // Settings struct updated — re-apply via backend if already on GPU
    m_settings = settings;
    if (m_initiated)
    {
      if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
      {
        backend->ApplyTextureSettings(this);
      }
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

  DepthTexture::~DepthTexture()
  {
    UnInit();
    Clear();
  }

  void DepthTexture::Load() {}

  void DepthTexture::Clear() { UnInit(); }

  int DepthTexture::GetFormatSize()
  {
    int internalFormatSize = m_stencil ? 4 : 3;

    int sizeMultiplier     = 1;
    if (m_settings.msaaCount > MsaaSampleCount::x0)
    {
      sizeMultiplier = (int) m_settings.msaaCount;
    }

    return internalFormatSize * sizeMultiplier;
  }

  void DepthTexture::Init(int width, int height, bool stencil, MsaaSampleCount multiSample)
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
      m_settings.msaaCount = MsaaSampleCount::x0;
    }

    IGraphicsBackend* backend = GetRenderSystem()->GetBackend();
    assert(backend && "Graphics backend not available during DepthTexture::Init");
    backend->CreateTexture(this);

    Stats::AddVRAMUsageInBytes((uint64) (m_width * m_height) * GetFormatSize());
  }

  void DepthTexture::UnInit()
  {
    if (m_gpuData == nullptr || !m_initiated)
    {
      return;
    }

    Stats::RemoveVRAMUsageInBytes((uint64) (m_width * m_height) * GetFormatSize());

    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->DestroyTexture(this);
    }

    m_initiated = false;
    m_stencil   = false;
  }

  GraphicTypes DepthTexture::GetDepthFormat()
  {
    return m_stencil ? GraphicTypes::FormatDepth24Stencil8 : GraphicTypes::FormatDepth24;
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

    assert(m_gpuData == nullptr && "Texture already initialized.");

    IGraphicsBackend* backend = GetRenderSystem()->GetBackend();
    assert(backend && "Graphics backend not available during DataTexture::Init");

    // Temporarily store data pointer in m_image so CreateTexture can upload it
    m_image = static_cast<uint8*>(data);
    backend->CreateTexture(this);
    backend->ApplyTextureSettings(this);
    m_image = nullptr; // DataTexture doesn't own the data

    Stats::AddVRAMUsageInBytes((uint64) (m_width * m_height) * BytesOfFormat(m_settings.InternalFormat));

    m_loaded    = true;
    m_initiated = true;
  };

  void DataTexture::Map(void* data)
  {
    if (!m_initiated)
    {
      assert(false && "Texture must be initialized before mapping data.");
      return;
    }

    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->UpdateTextureRegion(this, data);
    }
  }

  void DataTexture::UnInit()
  {
    if (m_gpuData == nullptr || !m_initiated)
    {
      return;
    }

    Stats::RemoveVRAMUsageInBytes((uint64) (m_width * m_height) * BytesOfFormat(m_settings.InternalFormat));

    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->DestroyTexture(this);
    }

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

    m_gpuData    = cubeMapTarget->m_gpuData; // Shared — both CubeMap and consumedRT use same GPU resource.
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

    assert(m_gpuData == nullptr && "Texture already initialized.");

    uint64 pixelCount         = (uint64) m_width * (uint64) m_height;
    IGraphicsBackend* backend = GetRenderSystem()->GetBackend();
    assert(backend && "Graphics backend not available during CubeMap::Init");

    backend->CreateTexture(this);
    Stats::AddVRAMUsageInBytes(pixelCount * 4 * 6); // Component count * face count.
    backend->ApplyTextureSettings(this);
    backend->GenerateMipmaps(this);

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
      m_consumedRT->m_gpuData.reset();
      m_consumedRT = nullptr;
    }

    Clear();
    m_initiated = false;
  }

  void CubeMap::AllocateMipMapStorage()
  {
    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->AllocateCubemapMipStorage(this);
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

  void Hdri::LoadOrGenerateIrradianceCaches()
  {
    if (m_waitingForInit)
    {
      // Guard for multiple generation requests.
      return;
    }

    String baseName = GenerateBakedEnvironmentFileBaseName();
    TrySettingCacheFiles(baseName);
    m_waitingForInit = true;

    if (_diffuseBakeFile.empty())
    {
      RenderTask task = {[this](Renderer* renderer) -> void
                         {
                           GenerateIrradianceCaches(renderer);
                           m_waitingForInit = false;
                         }};
      GetRenderSystem()->AddRenderTask(task);
    }
    else
    {
      RenderTask task = {[this](Renderer* renderer) -> void
                         {
                           LoadIrradianceCaches(renderer);
                           m_waitingForInit = false;

                           // Clear file path after initialization, otherwise init always loads from file.
                           // This isn't desired, only after load we should read from file. Settings changes should
                           // reflect during editor time or in game requests.
                           _diffuseBakeFile.clear();
                           _specularBakeFile.clear();
                         }};

      GetRenderSystem()->AddRenderTask(task);
    }
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

    // Clamp max mip level to the last baked level.
    if (IGraphicsBackend* backend = GetRenderSystem()->GetBackend())
    {
      backend->SetTextureMaxMipLevel(m_specularEnvMap.get(), RHIConstants::SpecularIBLLods - 1);
    }

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

    assert(m_gpuData == nullptr && "Texture already initialized.");

    IGraphicsBackend* backend = GetRenderSystem()->GetBackend();
    assert(backend && "Graphics backend not available during RenderTarget::Init");

    backend->CreateTexture(this);

    uint64 pixelCount = (uint64) m_width * (uint64) m_height;
    if (m_settings.Target == GraphicTypes::Target2D)
    {
      int msaaScale = m_settings.msaaCount > MsaaSampleCount::x0 ? (int) m_settings.msaaCount : 1;
      if (m_settings.msaaCount == MsaaSampleCount::x0)
      {
        backend->ApplyTextureSettings(this);
      }
      Stats::AddVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * msaaScale);
    }
    else if (m_settings.Target == GraphicTypes::TargetCubeMap)
    {
      backend->ApplyTextureSettings(this);
      Stats::AddVRAMUsageInBytes(pixelCount * BytesOfFormat(m_settings.InternalFormat) * 6);
    }
    else if (m_settings.Target == GraphicTypes::Target2DArray)
    {
      assert(m_settings.Layers > 0 && "Layer count must be at least 1");
      backend->ApplyTextureSettings(this);
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

  // TextureManager
  //////////////////////////////////////////

  TextureManager::TextureManager()
  {
    m_baseType         = Texture::StaticClass();
    m_defaultAOTexture = nullptr;
  }

  void TextureManager::Init()
  {
    ResourceManager::Init();

    // AO texture.
    TextureSettings settings;
    settings.Target         = GraphicTypes::Target2D;
    settings.MinFilter      = GraphicTypes::SampleNearest;
    settings.MagFilter      = GraphicTypes::SampleNearest;
    settings.WarpS          = GraphicTypes::UVClampToEdge;
    settings.WarpT          = GraphicTypes::UVClampToEdge;
    settings.InternalFormat = GraphicTypes::FormatR8;
    settings.Format         = GraphicTypes::FormatRed;
    settings.Type           = GraphicTypes::TypeUnsignedByte;
    settings.GenerateMipMap = false;

    ubyte* whitePixel       = new ubyte(255);
    RenderTargetPtr aoTex   = MakeNewPtr<RenderTarget>();
    aoTex->m_image          = whitePixel;
    aoTex->m_label          = "DefaultAOTexture";
    aoTex->m_name           = "DefaultAOTexture";
    aoTex->m_width          = 1;
    aoTex->m_height         = 1;
    aoTex->Settings(settings);
    aoTex->Init(true);
    m_defaultAOTexture = aoTex;

    Manage(aoTex);

    // Black texture.
    ubyte* blackPixel       = new ubyte(0);
    TexturePtr blackTexture = MakeNewPtr<Texture>();
    blackTexture->m_image   = blackPixel;
    blackTexture->m_label   = "DefaultBlackTexture";
    blackTexture->m_name    = "DefaultBlackTexture";
    blackTexture->m_width   = 1;
    blackTexture->m_height  = 1;
    blackTexture->Settings(settings);
    blackTexture->Init(true);
    m_blackTexture = blackTexture;

    Manage(blackTexture);
  }

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

  TexturePtr TextureManager::GetDefaultAOTexture() const { return m_defaultAOTexture; }

  TexturePtr TextureManager::GetBlackTexture() const { return m_blackTexture; }

} // namespace ToolKit
