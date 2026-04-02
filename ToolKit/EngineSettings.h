/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Object.h"
#include "Shader.h"

namespace ToolKit
{

  // WindowSettings
  //////////////////////////////////////////

  /**
   * Window settings class that holds all the window settings.
   * Only affects the application during application initialization.
   */
  class TK_API WindowSettings : public Object
  {
   public:
    TKDeclareClass(WindowSettings, Object);

   protected:
    void ParameterConstructor() override;

   public:
    /** Application window name. */
    TKDeclareParam(String, Name);

    /** Application window width for windowed mode. */
    TKDeclareParam(uint, Width);

    /** Application window height for windowed mode. */
    TKDeclareParam(uint, Height);

    /** States if the application is full screen or windowed. */
    TKDeclareParam(bool, FullScreen);
  };

  typedef std::shared_ptr<WindowSettings> WindowSettingsPtr;

  // ShadowSettings
  //////////////////////////////////////////

  /**
   * Shadow settings class that holds all the shadow settings.
   * It is used to configure the shadows at runtime.
   */
  class TK_API ShadowSettings : public Object
  {
   public:
    TKDeclareClass(ShadowSettings, Object);

    /** Returns last cascade's shadow camera far distance. */
    float GetShadowMaxDistance() const { return GetCascadeDistancesVal()[GetCascadeCountVal() - 1]; }

    /** Sets last cascade's shadow camera far distance. */
    void SetShadowMaxDistance(float distance)
    {
      Vec4 dist                      = GetCascadeDistancesVal();
      dist[GetCascadeCountVal() - 1] = distance;
      SetCascadeDistancesVal(dist);
    }

    /** Returns shadow atlas resolution based on Use2KShadowAtlasLayer setting. */
    int GetShadowAtlasResolution() const { return GetUse2KShadowAtlasLayerVal() ? 2048 : 1024; }

   protected:
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;
    void PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

    /** Updates graphics constant buffer with current settings. */
    ValueUpdateFn m_updateGraphicConstantsFn;

   public:
    /** Shadow cascade count. */
    TKDeclareParam(int, CascadeCount);

    /** Manual shadow cascade distances. */
    TKDeclareParam(Vec4, CascadeDistances);

    /** Near distance for shadow camera. */
    TKDeclareParam(float, ShadowMinDistance);

    /**
     * Cascade splitting will either use manual cascadeDistances or calculated ones. If this is true
     * cascadeDistances are calculated as a mix between logarithmic and linear split.
     */
    TKDeclareParam(bool, UseParallelSplitPartitioning);

    /** Linear mixture weight for parallel and linear splitting for cascades. */
    TKDeclareParam(float, ParallelSplitLambda);

    /** Prevents shimmering effects by preventing sub-pixel movement with the cost of wasted shadow map resolution. */
    TKDeclareParam(bool, StableShadowMap);

    /** Uses 2K resolution for shadow atlas layers instead of 1K. */
    TKDeclareParam(bool, Use2KShadowAtlasLayer);

    /**
     * Shadow PCF filtering sample count. Controls shadow softness.
     * 0: No filtering (single sample), 4: ~3x3 kernel, 9: ~5x5 kernel, 16: ~7x7 kernel.
     * This is a shader variant (compile-time define).
     */
    TKDeclareParam(MultiChoiceVariant, ShadowPCF);

    /** VSM blur kernel size applied to shadow atlas layers. */
    TKDeclareParam(MultiChoiceVariant, VSMBlurKernelSize);

    /** VSM blur tap count (number of horizontal+vertical blur passes). */
    TKDeclareParam(MultiChoiceVariant, VSMBlurTapCount);
  };

  typedef std::shared_ptr<ShadowSettings> ShadowSettingsPtr;

  // GraphicSettings
  //////////////////////////////////////////

  /**
   * Graphic settings class that holds all the graphic settings.
   * It is used to configure the graphics at runtime.
   */
  class TK_API GraphicSettings : public Object
  {
   public:
    TKDeclareClass(GraphicSettings, Object);

    /** Native constructor for the object. */
    void NativeConstruct() override
    {
      Super::NativeConstruct();
      m_shadows = MakeNewPtr<ShadowSettings>();
    }

   protected:
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;
    void PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

   public:
    /** Target fps for application. */
    TKDeclareParam(int, FPS);

    /** Multi-sample count. MsaaSampleCount::x1 for non msaa render targets. */
    TKDeclareParam(MultiChoiceVariant, MSAA);

    /** Disable msaa fully. Some hardware especially android emulators requires non msaa targets. */
    static constexpr bool disableMSAA = false;

    /** Sets render targets as floating point, allows values larger than 1.0 for HDR rendering. */
    TKDeclareParam(bool, HDRPipeline);

    /** When set to true, async tasks runs on actual threads and thread pools, otherwise they reside in task array. */
    TKDeclareParam(bool, MultiThreaded);

    /**
     * Viewport render target multiplier that adjusts the resolution.
     * High DPI devices such as mobile phones benefits from this.
     */
    TKDeclareParam(float, RenderResolutionScale);

    /** Anisotropic texture filtering value. It can be 0, 2 ,4, 8, 16. Clamped with gpu max anisotropy. */
    TKDeclareParam(MultiChoiceVariant, AnisotropicTextureFiltering);

    /** When set to true, graphics settings saved as shader defines. Preventing compiling all define configurations.  */
    bool m_saveShaderDefines = true;

    /** Global shadow settings. */
    ShadowSettingsPtr m_shadows;
  };

  typedef std::shared_ptr<GraphicSettings> GraphicSettingsPtr;

  // PostProcessingSettings
  //////////////////////////////////////////

  /**
   * Post processing settings class that holds all the post processing settings.
   * It is used to configure the post processing at runtime.
   */
  class TK_API PostProcessingSettings : public Object
  {
   public:
    TKDeclareClass(PostProcessingSettings, Object);

    // Tone mapping
    /////////////////////

    TKDeclareParam(bool, TonemappingEnabled);
    TKDeclareParam(MultiChoiceVariant, TonemapperMode);

    // Bloom
    /////////////////////
    TKDeclareParam(bool, BloomEnabled);
    TKDeclareParam(float, BloomIntensity);
    TKDeclareParam(float, BloomThreshold);
    TKDeclareParam(int, BloomIterationCount);

    // Gamma
    /////////////////////
    TKDeclareParam(bool, GammaCorrectionEnabled);
    TKDeclareParam(float, Gamma);

    // SSAO
    /////////////////////
    TKDeclareParam(bool, SSAOEnabled);
    TKDeclareParam(float, SSAORadius);
    TKDeclareParam(float, SSAOBias);
    TKDeclareParam(float, SSAOSpread);
    TKDeclareParam(int, SSAOKernelSize);

    // DOF
    /////////////////////
    TKDeclareParam(bool, DepthOfFieldEnabled);
    TKDeclareParam(float, FocusPoint);
    TKDeclareParam(float, FocusScale);
    TKDeclareParam(int, DofBlurQuality);

    // Anti-aliasing
    /////////////////////
    TKDeclareParam(bool, FXAAEnabled);

   protected:
    void ParameterConstructor() override;
  };

  typedef std::shared_ptr<PostProcessingSettings> PostProcessingSettingsPtr;

  // ShaderSettings
  //////////////////////////////////////////

  /**
   * Compiling all shader define combinations leads to long compile times. These settings makes an upfront choice for
   * each define if present here. Compile will not perform full define set compilation, instead only the existing define
   * set will be compiled for the provided values. This can help to create predefined categories like "Low", "Medium",
   * "High" or "Ultra" settings. Shader compile works only for the chosen defines and their values.
   */
  class TK_API ShaderSettings : public Object
  {
   public:
    TKDeclareClass(ShaderSettings, Object);

    /** If there exist presets for the defines of the given shader, syncs the input defines array with presets. */
    void SyncDefinesForShader(const String& shaderPath, ShaderDefineArray& defines) const;

    /** Add or set a shader define to the list for the given shader path. */
    void SetShaderDefine(const String& shaderPath, const ShaderDefine& define);

    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

   public:
    /**
     * Map of shader defines and their values. Key is a shader file path. If specific defines provided, instead of full
     * define range, the given values are used to compile the shader at initialization.
     */
    std::map<String, ShaderDefineArray> m_shaderDefines;
  };

  typedef std::shared_ptr<ShaderSettings> ShaderSettingsPtr;

  // EngineSettings
  //////////////////////////////////////////

  /**
   * Engine settings class that holds all the engine settings.
   * It is serialized to a file and can be loaded from it.
   * It is used to configure the engine at runtime.
   */
  class TK_API EngineSettings : public Serializable
  {
   public:
    EngineSettings();

    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

    void Save(const String& path);
    void Load(const String& path);

   protected:
    /** Sets shader defines for current graphics settings. */
    void SetShaderSettings();

   public:
    WindowSettingsPtr m_window;
    GraphicSettingsPtr m_graphics;
    PostProcessingSettingsPtr m_postProcessing;
    ShaderSettingsPtr m_shaderSettings;
    StringSet m_loadedPlugins;
  };

} // namespace ToolKit