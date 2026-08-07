/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Material.h"

#include "FileManager.h"
#include "Logger.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "Shader.h"
#include "Stats.h"
#include "ToolKit.h"
#include "Util.h"

#include "DebugNew.h"

namespace ToolKit
{

  // Material
  //////////////////////////////////////////

  TKDefineClass(Material, Resource);

  Material::Material() { m_usingDefaultShaders = true; }

  Material::Material(const String& file) : Material() { SetFile(file); }

  Material::~Material() { UnInit(); }

  void Material::Load()
  {
    if (!m_loaded)
    {
      ParseDocument("material");
      m_loaded = true;
    }
  }

  void Material::Save(bool onlyIfDirty)
  {
    Resource::Save(onlyIfDirty);

    if (ShaderPtr shader = GetFragmentShaderVal())
    {
      shader->Save(onlyIfDirty);
    }

    if (ShaderPtr shader = GetVertexShaderVal())
    {
      shader->Save(onlyIfDirty);
    }
  }

  void Material::Init(bool flushClientSideArray)
  {
    if (m_initiated)
    {
      return;
    }

    if (TexturePtr tex = GetDiffuseTextureVal())
    {
      tex->Init(flushClientSideArray);
    }

    if (TexturePtr tex = GetEmissiveTextureVal())
    {
      tex->Init(flushClientSideArray);
    }

    if (TexturePtr tex = GetMetallicRoughnessTextureVal())
    {
      MakeSureItsDataTexture(tex);
    }

    if (TexturePtr tex = GetNormalTextureVal())
    {
      MakeSureItsDataTexture(tex);
    }

    if (m_cubeMap)
    {
      m_cubeMap->Init(flushClientSideArray);
    }

    GetVertexShaderVal()->Init();
    GetFragmentShaderVal()->Init();

    m_initiated = true;
  }

  void Material::UnInit() { m_initiated = false; }

  void Material::CopyTo(Resource* other)
  {
    Super::CopyTo(other);
    Material* cpy              = static_cast<Material*>(other);
    cpy->m_cubeMap             = m_cubeMap;
    cpy->cullMode              = cullMode;
    cpy->blendFunction         = blendFunction;
    cpy->drawType              = drawType;
    cpy->alphaMaskTreshold     = alphaMaskTreshold;
    cpy->lineWidth             = lineWidth;
    cpy->m_usingDefaultShaders = m_usingDefaultShaders;
    cpy->m_dirty               = true;
  }

  bool Material::IsTranslucent()
  {
    switch (blendFunction)
    {
      case BlendFunction::NONE:
      case BlendFunction::ALPHA_MASK:
        return false;
      case BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA:
      case BlendFunction::ONE_TO_ONE:
        return true;
      default:
        return false;
    }
  }

  bool Material::IsAlphaMasked() { return blendFunction == BlendFunction::ALPHA_MASK; }

  void Material::SetBlendState(BlendFunction blendState)
  {
    if (blendFunction == blendState)
    {
      return;
    }

    blendFunction = blendState;

    if (blendState == BlendFunction::NONE)
    {
      SetAlphaVal(1.0f); // Make the material fully opaque.
    }

    m_materialCacheItem.Invalidate();
    m_dirty = true;
  }

  void Material::SetAlphaMaskThreshold(float val)
  {
    alphaMaskTreshold = val;
    m_materialCacheItem.Invalidate();
    m_dirty = true;
  }

  bool Material::IsPBR()
  {
    // If using default shaders, its a pbr material.
    return m_usingDefaultShaders;
  }

  bool Material::IsShaderMaterial() { return !m_usingDefaultShaders; }

  XmlNode* Material::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    parent                   = Super::SerializeImp(doc, parent);

    XmlNode* container       = CreateXmlNode(doc, Material::StaticClass()->Name, parent);

    // Active rasterizer state. Asset format keeps the <renderState> wrapper for backwards
    // compatibility with existing material files; in memory the fields live directly on Material.
    XmlNode* renderStateNode = doc->allocate_node(rapidxml::node_type::node_element, "renderState");
    container->append_node(renderStateNode);
    WriteAttr(renderStateNode, doc, "cullMode", std::to_string(int(cullMode)));
    WriteAttr(renderStateNode, doc, "blendFunction", std::to_string(int(blendFunction)));
    WriteAttr(renderStateNode, doc, "drawType", std::to_string(int(drawType)));
    WriteAttr(renderStateNode, doc, "alphaMaskTreshold", std::to_string((float) alphaMaskTreshold));

    return container;
  }

  XmlNode* Material::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    if (m_version == TKV049)
    {
      DeSerializeImpV049(info, parent);
      return nullptr;
    }

    XmlNode* rootNode = parent;
    for (XmlNode* node = rootNode->first_node(); node; node = node->next_sibling())
    {
      if (strcmp("diffuseTexture", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);

        TexturePtr tex = GetTextureManager()->Create<Texture>(TexturePath(path));
        SetDiffuseTextureVal(tex);
      }
      else if (strcmp("emissiveTexture", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);

        TexturePtr tex = GetTextureManager()->Create<Texture>(TexturePath(path));
        SetEmissiveTextureVal(tex);
      }
      else if (strcmp("metallicRoughnessTexture", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);

        TexturePtr tex = GetTextureManager()->Create<Texture>(TexturePath(path));
        SetMetallicRoughnessTextureVal(tex);
      }
      else if (strcmp("normalMap", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);

        TexturePtr tex = GetTextureManager()->Create<Texture>(TexturePath(path));
        SetNormalTextureVal(tex);
      }
      else if (strcmp("shader", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);
        if (ShaderPtr shader = GetShaderManager()->Create<Shader>(ShaderPath(path)))
        {
          if (shader->m_shaderType == ShaderType::VertexShader)
          {
            SetVertexShaderVal(shader);
          }
          else if (shader->m_shaderType == ShaderType::FragmentShader)
          {
            SetFragmentShaderVal(shader);
          }
          else
          {
            assert(false && "Shader type is not supported.");
          }
        }
      }
      else if (strcmp("renderState", node->name()) == 0)
      {
        ReadRenderStateXml(node);
      }
      else
      {
        TK_WRN("Unknown material param: %s", node->name());
      }
    }

    return nullptr;
  }

  void Material::PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    Super::PostDeSerializeImp(info, parent);
    CheckDefaultShaders();
  }

  void Material::DeSerializeImpV049(const SerializationFileInfo& info, XmlNode* parent)
  {
    parent            = Super::DeSerializeImp(info, parent);
    XmlNode* rootNode = parent->first_node(StaticClass()->Name.c_str());
    for (XmlNode* node = rootNode->first_node(); node; node = node->next_sibling())
    {
      if (strcmp("renderState", node->name()) == 0)
      {
        ReadRenderStateXml(node);
      }
      else
      {
        TK_WRN("Unknown material param: %s", node->name());
      }
    }
  }

  void Material::ReadRenderStateXml(XmlNode* renderStateNode)
  {
    // Reads the active rasterizer fields from a <renderState ... /> attribute block. Legacy passive
    // attributes (depthTest, depthFunc) that may exist on old assets are intentionally ignored —
    // passive state is now owned by the pass, not the material.
    int cullModeInt = (int) cullMode;
    ReadAttr(renderStateNode, "cullMode", cullModeInt);
    switch ((CullingType) cullModeInt)
    {
      case CullingType::TwoSided:
      case CullingType::Front:
      case CullingType::Back:
        cullMode = (CullingType) cullModeInt;
        break;
      default:
        cullMode = CullingType::Back;
        break;
    }

    int blendInt = (int) blendFunction;
    ReadAttr(renderStateNode, "blendFunction", blendInt);
    switch ((BlendFunction) blendInt)
    {
      case BlendFunction::NONE:
      case BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA:
      case BlendFunction::ALPHA_MASK:
      case BlendFunction::ONE_TO_ONE:
        blendFunction = (BlendFunction) blendInt;
        break;
      default:
        blendFunction = BlendFunction::NONE;
        break;
    }

    int drawInt = (int) drawType;
    ReadAttr(renderStateNode, "drawType", drawInt);
    switch ((DrawType) drawInt)
    {
      case DrawType::Triangle:
      case DrawType::Line:
      case DrawType::LineStrip:
      case DrawType::LineLoop:
      case DrawType::Point:
        drawType = (DrawType) drawInt;
        break;
      default:
        drawType = DrawType::Triangle;
        break;
    }

    ReadAttr(renderStateNode, "alphaMaskTreshold", alphaMaskTreshold);
  }

  void Material::ParameterConstructor()
  {
    Super::ParameterConstructor();

    ShaderManager* sman = GetShaderManager();
    VertexShader_Define(sman->GetDefaultVertexShader(), MaterialCategory.Name, MaterialCategory.Priority, false, false);
    FragmentShader_Define(sman->GetPbrForwardShader(), MaterialCategory.Name, MaterialCategory.Priority, false, false);

    DiffuseTexture_Define(nullptr, MaterialCategory.Name, MaterialCategory.Priority, false, false);
    EmissiveTexture_Define(nullptr, MaterialCategory.Name, MaterialCategory.Priority, false, false);
    NormalTexture_Define(nullptr, MaterialCategory.Name, MaterialCategory.Priority, false, false);
    MetallicRoughnessTexture_Define(nullptr, MaterialCategory.Name, MaterialCategory.Priority, false, false);

    Alpha_Define(1.0f, MaterialCategory.Name, MaterialCategory.Priority, true, false);
    Metallic_Define(0.2f, MaterialCategory.Name, MaterialCategory.Priority, true, false);
    Roughness_Define(0.5f, MaterialCategory.Name, MaterialCategory.Priority, true, false);
    Color_Define(Vec3(1.0f), MaterialCategory.Name, MaterialCategory.Priority, true, false);
    EmissiveColor_Define(Vec3(0.0f), MaterialCategory.Name, MaterialCategory.Priority, true, false);
  }

  void Material::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    ParamVertexShader().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          m_materialCacheItem.Invalidate();
          m_dirty = true;

          CheckDefaultShaders();
        });

    ParamFragmentShader().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          m_materialCacheItem.Invalidate();
          m_dirty = true;

          CheckDefaultShaders();
        });

    ParamDiffuseTexture().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          TexturePtr tex                               = std::get<TexturePtr>(newVal);
          m_materialCacheItem.data.metallicRoughness.w = tex != nullptr ? 1.0f : 0.0f;
          m_materialCacheItem.Invalidate();
          m_dirty = true;
        });

    ParamEmissiveTexture().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          TexturePtr tex                          = std::get<TexturePtr>(newVal);
          m_materialCacheItem.data.textureFlags.x = tex != nullptr ? 1.0f : 0.0f;
          m_materialCacheItem.Invalidate();
          m_dirty = true;
        });

    ParamMetallicRoughnessTexture().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          TexturePtr tex = std::get<TexturePtr>(newVal);
          MakeSureItsDataTexture(tex);
          m_materialCacheItem.data.textureFlags.z = tex != nullptr ? 1.0f : 0.0f;
          m_materialCacheItem.Invalidate();
          m_dirty = true;
        });

    ParamNormalTexture().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          TexturePtr tex = std::get<TexturePtr>(newVal);
          MakeSureItsDataTexture(tex);
          m_materialCacheItem.data.textureFlags.y = tex != nullptr ? 1.0f : 0.0f;
          m_materialCacheItem.Invalidate();
          m_dirty = true;
        });

    ParamAlpha().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          float val        = std::get<float>(newVal);
          val              = glm::clamp(val, 0.0f, 1.0f);
          newVal           = val;

          bool translucent = val < 0.999f;
          if (translucent && blendFunction != BlendFunction::ALPHA_MASK)
          {
            blendFunction = BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA;
          }

          m_materialCacheItem.data.colorAlpha.a = val;
          m_materialCacheItem.Invalidate();
          m_dirty = true;
        });

    ParamMetallic().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          float val                                    = std::get<float>(newVal);
          m_materialCacheItem.data.metallicRoughness.x = val;
          m_materialCacheItem.Invalidate();
          m_dirty = true;
        });

    ParamRoughness().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          float val                                    = std::get<float>(newVal);
          m_materialCacheItem.data.metallicRoughness.y = val;
          m_materialCacheItem.Invalidate();
          m_dirty = true;
        });

    ParamColor().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          Vec3 val                            = std::get<Vec3>(newVal);
          m_materialCacheItem.data.colorAlpha = Vec4(val, GetAlphaVal());
          m_materialCacheItem.Invalidate();
          m_dirty = true;
        });

    ParamEmissiveColor().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          Vec3 val                                   = std::get<Vec3>(newVal);
          m_materialCacheItem.data.emissiveThreshold = Vec4(val, alphaMaskTreshold);
          m_materialCacheItem.Invalidate();
          m_dirty = true;
        });
  }

  void Material::CheckDefaultShaders()
  {
    m_usingDefaultShaders = GetVertexShaderVal() == GetShaderManager()->GetDefaultVertexShader() &&
                            GetFragmentShaderVal() == GetShaderManager()->GetPbrForwardShader();
  }

  void Material::MakeSureItsDataTexture(TexturePtr texture)
  {
    if (texture == nullptr)
    {
      return;
    }

    const TextureSettings& current = texture->Settings();
    TextureSettings dataSet        = current;
    dataSet.MinFilter              = GraphicTypes::SampleLinearMipmapLinear;
    dataSet.MagFilter              = GraphicTypes::SampleLinear;
    dataSet.WarpS                  = GraphicTypes::UVRepeat;
    dataSet.WarpT                  = GraphicTypes::UVRepeat;
    dataSet.GenerateMipMap         = true;
    // Normal map / metallic-roughness textures hold numeric data, not perceptual color: sample
    // values must reach the shader in linear space. The default texture pipeline tags everything
    // as FormatSRGB8_A8, which makes the GPU apply an sRGB→linear curve on sample and warps
    // both the (x,y,z) of the normal and the metallic/roughness scalars — observed as bogus
    // specular highlights on imported PBR models (worse on Vulkan because its sampler applies
    // the curve in hardware strictly; some GL drivers were forgiving). Force linear RGBA8 here.
    // This is a one-shot re-init triggered by Material::Init, mirroring the GL main-branch
    // behavior the user described.
    if (dataSet.InternalFormat == GraphicTypes::FormatSRGB8_A8)
    {
      dataSet.InternalFormat = GraphicTypes::FormatRGBA8;
    }

    if (current.MinFilter == dataSet.MinFilter && current.WarpS == dataSet.WarpS && current.WarpT == dataSet.WarpT &&
        current.GenerateMipMap == dataSet.GenerateMipMap && current.InternalFormat == dataSet.InternalFormat)
    {
      if (texture->m_initiated)
      {
        return;
      }
    }

    bool flush = current.Type == GraphicTypes::TypeFloat ? texture->m_imagef == nullptr : texture->m_image == nullptr;

    texture->UnInit();
    if (flush)
    {
      texture->m_loaded = false;
      texture->Load();
    }

    texture->Settings(dataSet);
    texture->Init(flush);
  }

  const MaterialCacheItem& Material::GetCacheItem()
  {
    if (m_materialCacheItem.isValid)
    {
      return m_materialCacheItem;
    }

    // Update the cache fields
    m_materialCacheItem.id                     = GetIdVal();
    m_materialCacheItem.data.colorAlpha        = Vec4(GetColorVal(), GetAlphaVal());
    m_materialCacheItem.data.emissiveThreshold = Vec4(GetEmissiveColorVal(), alphaMaskTreshold);

    m_materialCacheItem.data.metallicRoughness = Vec4(GetMetallicVal(),
                                                      GetRoughnessVal(),
                                                      (blendFunction == BlendFunction::ALPHA_MASK) ? 1.0f : 0.0f,
                                                      (GetDiffuseTextureVal() != nullptr) ? 1.0f : 0.0f);

    m_materialCacheItem.data.textureFlags      = Vec4((GetEmissiveTextureVal() != nullptr) ? 1.0f : 0.0f,
                                                      (GetNormalTextureVal() != nullptr) ? 1.0f : 0.0f,
                                                      (GetMetallicRoughnessTextureVal() != nullptr) ? 1.0f : 0.0f,
                                                      0.0f // Padding
    );

    // Validate the cache
    m_materialCacheItem.Validate();

    return m_materialCacheItem;
  }

  void Material::InvalidateCacheItem() { m_materialCacheItem.Invalidate(); }

  MaterialManager::MaterialManager() { m_baseType = Material::StaticClass(); }

  MaterialManager::~MaterialManager() {}

  void MaterialManager::Init()
  {
    ResourceManager::Init();

    // PBR material
    MaterialPtr material     = MakeNewPtr<Material>();
    ShaderManager* shaderMan = GetShaderManager();
    ShaderPtr defVertex      = shaderMan->Create<Shader>(ShaderPath("defaultVertex.shader", true));
    material->SetVertexShaderVal(defVertex);
    material->SetFragmentShaderVal(shaderMan->GetPbrForwardShader());

    TextureManager* texMan = GetTextureManager();
    material->SetDiffuseTextureVal(texMan->Create<Texture>(TexturePath(TKDefaultImage, true)));
    material->Init();
    m_defaultMaterial                                 = material;
    m_storage[MaterialPath("default.material", true)] = material;

    // Unlit material
    material                                          = MakeNewPtr<Material>();
    material->SetVertexShaderVal(defVertex);
    material->SetFragmentShaderVal(shaderMan->Create<Shader>(ShaderPath("unlitFrag.shader", true)));

    material->SetDiffuseTextureVal(texMan->Create<Texture>(TexturePath(TKDefaultImage, true)));
    material->Init();
    m_storage[MaterialPath("unlit.material", true)] = material;
  }

  bool MaterialManager::CanStore(ClassMeta* Class) { return Class == Material::StaticClass(); }

  String MaterialManager::GetDefaultResource(ClassMeta* Class) { return MaterialPath("missing.material", true); }

  MaterialPtr MaterialManager::GetDefaultMaterial() { return m_defaultMaterial; }

  MaterialPtr MaterialManager::GetCopyOfUnlitMaterial(bool storeInMaterialManager)
  {
    String file        = MaterialPath("unlit.material", true);
    ResourcePtr source = m_storage[file];
    return Copy<Material>(source, storeInMaterialManager);
  }

  MaterialPtr MaterialManager::GetCopyOfUIMaterial(bool storeInMaterialManager)
  {
    MaterialPtr material    = GetMaterialManager()->GetCopyOfUnlitMaterial(storeInMaterialManager);
    material->blendFunction = BlendFunction::ALPHA_MASK;

    return material;
  }

  MaterialPtr MaterialManager::GetCopyOfUnlitColorMaterial(bool storeInMaterialManager)
  {
    MaterialPtr umat = GetCopyOfUnlitMaterial(storeInMaterialManager);
    umat->SetDiffuseTextureVal(nullptr);

    return umat;
  }

  MaterialPtr MaterialManager::GetCopyOfDefaultMaterial(bool storeInMaterialManager)
  {
    String file        = MaterialPath("default.material", true);
    ResourcePtr source = m_storage[file];
    return Copy<Material>(source, storeInMaterialManager);
  }

} // namespace ToolKit
