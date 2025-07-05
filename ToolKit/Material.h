/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "GenericBuffers.h"
#include "RenderState.h"
#include "Resource.h"
#include "ResourceManager.h"
#include "ShaderUniform.h"
#include "Texture.h"
#include "UniformBuffer.h"

namespace ToolKit
{

  // MaterialCacheItem
  //////////////////////////////////////////

  struct TK_API MaterialCacheItem : CacheItem
  {
    /** Uniforms for material in std140 layout. */
    struct Data
    {
      Vec4 colorAlpha;        //!< rgb=color, a=alpha
      Vec4 emissiveThreshold; //!< rgb=emissive, a=alphaThreshold
      Vec4 metallicRoughness; //!< x=metallic, y=roughness, z=useAlphaMask, w=diffuseInUse
      Vec4 textureFlags;      //!< x=emissive, y=normal, z=metallicRough, w=pad
    } data;

    bool DiffuseTextureInUse() const { return data.metallicRoughness.w > 0.5f; }

    bool EmissiveTextureInUse() const { return data.textureFlags.x > 0.5f; }

    bool NormalTextureInUse() const { return data.textureFlags.y > 0.5f; }

    bool MetallicRoughnessTextureInUse() const { return data.textureFlags.z > 0.5f; }

    void* GetData() override { return (void*) &data; }
  };

  // Material
  //////////////////////////////////////////

  static VariantCategory MaterialCategory {"Material", 100};

  class TK_API Material : public Resource, public ICacheable
  {
   public:
    TKDeclareClass(Material, Resource);

    Material();
    Material(const String& file);
    virtual ~Material();

    void Load() override;
    void Save(bool onlyIfDirty) override;
    void Init(bool flushClientSideArray = false) override;
    void UnInit() override;
    RenderState* GetRenderState();
    void SetRenderState(RenderState* state);

    /**
     * States if the material has transparency.
     * @returns True if the blend state is SRC_ALPHA_ONE_MINUS_SRC_ALPHA.
     */
    bool IsTranslucent();

    /** States if the material is alpha masked. */
    bool IsAlphaMasked();

    /** Sets blend state. */
    void SetBlendState(BlendFunction blendState);

    /** Sets alpha mask threshold. */
    void SetAlphaMaskThreshold(float val);

    /**
     * States if the material is using PBR shaders.
     * @returns True if the fragment shader is default PBR shader.
     */
    bool IsPBR();

    /** Returns true if not using default shaders. */
    bool IsShaderMaterial();

    /** Shader materials can update their uniforms via this function. */
    void UpdateProgramUniform(const String& uniformName, const UniformValue& val);

    const MaterialCacheItem& GetCacheItem() override;

    void InvalidateCacheItem() override;

   protected:
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;
    void PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

    /** Deserialize files with version v0.4.9 */
    void DeSerializeImpV049(const SerializationFileInfo& info, XmlNode* parent);

    void ParameterConstructor() override;
    void ParameterEventConstructor() override;

    /** Checks if the material is using default shaders and update m_usingDefaultShaders. */
    void CheckDefaultShaders();

   private:
    void CopyTo(Resource* other) override;

   public:
    CubeMapPtr m_cubeMap;

    TKDeclareParam(ShaderPtr, VertexShader);
    TKDeclareParam(ShaderPtr, FragmentShader);

    TKDeclareParam(TexturePtr, DiffuseTexture);
    TKDeclareParam(TexturePtr, EmissiveTexture);
    TKDeclareParam(TexturePtr, MetallicRoughnessTexture);
    TKDeclareParam(TexturePtr, NormalTexture);

    TKDeclareParam(float, Alpha);
    TKDeclareParam(float, Metallic);
    TKDeclareParam(float, Roughness);
    TKDeclareParam(Vec3, Color);
    TKDeclareParam(Vec3, EmissiveColor);

   private:
    RenderState m_renderState;

    /** Gpu representation of the material. */
    MaterialCacheItem m_materialCacheItem;

    /** States if the material is using default shaders. */
    bool m_usingDefaultShaders;
  };

  // MaterialManager
  //////////////////////////////////////////

  class TK_API MaterialManager : public ResourceManager
  {
   public:
    MaterialManager();
    virtual ~MaterialManager();
    void Init() override;
    bool CanStore(ClassMeta* Class) override;
    String GetDefaultResource(ClassMeta* Class) override;
    MaterialPtr GetDefaultMaterial();
    MaterialPtr GetCopyOfUnlitMaterial(bool storeInMaterialManager = true);
    MaterialPtr GetCopyOfUIMaterial(bool storeInMaterialManager = true);
    MaterialPtr GetCopyOfUnlitColorMaterial(bool storeInMaterialManager = true);
    MaterialPtr GetCopyOfDefaultMaterial(bool storeInMaterialManager = true);

   private:
    MaterialPtr m_defaultMaterial = nullptr;
  };

} // namespace ToolKit
