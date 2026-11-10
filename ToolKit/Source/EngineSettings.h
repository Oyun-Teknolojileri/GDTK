/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Object.h"
#include "Shader.h"

#include <map>

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

    /** Shadow atlas layer resolutions the editor offers. */
    static constexpr int AtlasResolution1K = 1024;
    static constexpr int AtlasResolution2K = 2048;
    static constexpr int AtlasResolution4K = 4096;

    /** Returns the shadow atlas layer resolution, kept within the offered choices. */
    int GetShadowAtlasResolution() const
    {
      const int resolution = GetShadowAtlasResolutionVal();
      if (resolution == AtlasResolution2K || resolution == AtlasResolution4K)
      {
        return resolution;
      }
      return AtlasResolution1K;
    }

    /**
     * Returns the slot density of the shadow atlas. Scale 1 is the default layout, scale 2 halves
     * every slot's resolution and therefore doubles the slot grid on each axis, which fits four
     * slots where one used to while the atlas texture keeps its size. Must stay within
     * ShadowAtlas::MaxSlotScale.
     */
    int GetShadowAtlasSlotScale() const { return GetUse2XShadowAtlasSlotsVal() ? TwoXSlotScale : DefaultSlotScale; }

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

    /**
     * Blends the two directional cascades across the far end of the nearer one, so a split does not
     * read as a straight line across the ground. The band is a fixed fraction of the cascade depth
     * span, set inside lighting.shader, and the weight is not dithered: a dithered weight would
     * reintroduce the temporal shimmer the stable fit removes. Costs a second shadow lookup inside
     * the band only, and compiles out entirely when off.
     */
    TKDeclareParam(bool, CascadeBlend);

    /**
     * Master switch for shadow casting.
     *
     * Off means the shadow pass renders nothing at all and the forward shader compiles its shadow
     * sampling out behind a CastShadow define, so a scene that needs no shadows pays neither the
     * atlas render nor the per pixel cost. Before this the same result needed the per light
     * CastShadow flags and the ShadowPCF and CascadeBlend settings edited by hand, which also
     * changed what shadows looked like once they came back. On keeps the previous behaviour: a
     * light casts when its own CastShadow is set.
     */
    TKDeclareParam(bool, CastShadow);

    /** Layer resolution of the shadow atlas: 1K, 2K or 4K. See GetShadowAtlasResolution. */
    TKDeclareParam(int, ShadowAtlasResolution);

    /**
     * Halves the resolution of every shadow atlas slot, which doubles the slot grid on each axis:
     * four slots fit where one used to (16 half, 48 quarter and 64 eighth instead of 4, 12 and 16).
     * The atlas keeps its size, so this trades shadow sharpness for the number of lights that can
     * cast a shadow.
     */
    TKDeclareParam(bool, Use2XShadowAtlasSlots);

    /** Slot density of the default layout. */
    static constexpr int DefaultSlotScale = 1;

    /** Slot density of the dense layout that Use2XShadowAtlasSlots selects. */
    static constexpr int TwoXSlotScale    = 2;

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
    /**
     * Target fps for application. The engine gates its frame on this value, independently of the
     * display's swap interval, so a run that asks for no pacing still has to unlock it here. Zero means
     * unlocked: every iteration draws and the frame rate is whatever the work costs.
     */
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
     * Draws runs of objects that share a mesh and a material as one instanced draw. Turning it off
     * falls back to one draw per object, which is what the engine does when a run cannot share a draw
     * anyway, and makes the cost of the batching measurable from the editor.
     */
    TKDeclareParam(bool, InstancedBatching);

    /**
     * Culls local lights and meshes against a coarse depth grid built from the frame's depth buffer. A
     * light whose whole volume sits behind what was already drawn contributes nothing to the picture, so
     * it is dropped from the frame: no slot in the shadow atlas, and no shadow map rendered for it. A mesh
     * whose whole bounding box sits behind what was already drawn is left out of the visible job list, so
     * it is neither batched nor drawn.
     *
     * The grid is one level: a maximum depth per eight by eight block, with no mip chain. It lags a frame
     * by design, since it is reduced from a depth buffer the frame cannot keep writing to, so a light or a
     * mesh is dropped only after it stayed hidden for three frames in a row and the counters reset when
     * the camera jumps. Geometry that deforms without its bounds following - a skinned mesh, a shader
     * material that places its own vertices - is never dropped.
     *
     * Turning the setting off restores the unculled behaviour, which is what makes the cost measurable
     * from the editor.
     */
    TKDeclareParam(bool, OcclusionCulling);

    /**
     * Whether the depth grid is carried into the frame's own view before it is tested, instead of being trusted in the
     * screen space of the frame that produced it. A grid is at least one frame old, so without this a camera that moved
     * slides the picture across the blocks and volumes are dropped while the sky shows through where they were. With it,
     * the blocks that no reprojected depth lands on are treated as unknown and the volume is kept.
     *
     * Turning it off restores the cheaper, stale grid behaviour - useful to see the difference, and to fall back to if a
     * scene ever reprojects badly.
     */
    TKDeclareParam(bool, OcclusionReprojection);

    /**
     * Viewport render target multiplier that adjusts the resolution.
     * High DPI devices such as mobile phones benefits from this.
     */
    TKDeclareParam(float, RenderResolutionScale);

    /** Anisotropic texture filtering value. It can be 0, 2 ,4, 8, 16. Clamped with gpu max anisotropy. */
    TKDeclareParam(MultiChoiceVariant, AnisotropicTextureFiltering);

    /**
     * Builds the normal and depth target - what ambient occlusion and depth of field read - at half
     * the frame resolution.
     *
     * Both consumers work at half resolution anyway, so a full size target is four times the
     * fragments for a picture neither of them can see. Screen space reflections read it at full
     * resolution and force this off themselves. Worth trying the other way on a desktop gpu, where
     * the target can also act as a depth prepass for the forward pass.
     */
    TKDeclareParam(bool, HalfResolutionForwardPreProcess);

    /**
     * Scale of the variance the geometric specular anti-aliasing term builds from the screen space
     * derivative of the shading normal. The term widens the specular lobe where the normal varies
     * faster than a pixel can show, which is what keeps a highlight narrower than a pixel from
     * blinking as the camera moves. Higher is steadier and softer, zero turns the term off.
     *
     * Two is what this engine's content needs, and the default: the city normal map is a flat field
     * with hard edged detail islands, so a pixel crossing an island border sees a normal jump.
     * Measured on that scene, eleven frame pairs with the camera animation playing, counting bright
     * pixels that change by more than 35 steps between two frames: 0.15, Filament's number, leaves 218;
     * 0.25, Godot's amount, 193; 2.0 gives 102, and 4.0 only takes a further 19.
     */
    TKDeclareParam(float, SpecularAAAmount);

    /**
     * Ceiling on the squared roughness the term above may add, so a normal jump cannot turn an
     * arbitrary amount of a smooth surface rough. Raising this on its own changes little (0.25 to 1.0
     * measured -7%), the amount is what decides; this bounds the damage one bad pixel can do.
     */
    TKDeclareParam(float, SpecularAALimit);

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

    /** How the glow levels are weighted: 1 spreads the energy evenly over every level, lower values
        pull it toward the tight ones. The reach of the halo stays the iteration count's job. */
    TKDeclareParam(float, BloomRadius);

    /** Bicubic reconstruction of the glow level the composite reads. Off samples it with a 2x2 box. */
    TKDeclareParam(bool, BloomBicubicUpscale);

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

    /**
     * How much of the occlusion map the lighting takes: 1 leaves it as it is, 0 turns the darkening
     * off without recompiling, 2 doubles it. The map itself is rarely strong enough to read on its own
     * - a pass that is correct can still be invisible - so the strength is a portrait knob, not part of
     * the sampling.
     */
    TKDeclareParam(float, SSAOStrength);
    TKDeclareParam(MultiChoiceVariant, SSAOKernelSize);
    TKDeclareParam(bool, SSAOHalfResolution);

    // SSR
    /////////////////////
    TKDeclareParam(bool, SSREnabled);
    TKDeclareParam(float, SSRIntensity);
    TKDeclareParam(float, SSRMaxDistance);
    TKDeclareParam(float, SSRThickness);
    TKDeclareParam(int, SSRStepCount);
    TKDeclareParam(float, SSRRoughnessCutoff);
    /**
     * Width of the screen border the reflections fade out across, as a fraction of the frame's width plus
     * height. The depth buffer stops holding the geometry a ray needs at the border, so a hit there is not
     * worth trusting.
     */
    TKDeclareParam(float, SSREdgeFade);
    /**
     * Where the fade along the ray's screen length starts, as a fraction of the length the march can reach.
     * The last reachable pixel of a ray ends in a hard edge without it.
     */
    TKDeclareParam(float, SSRScreenFadeStart);
    /**
     * Where the fade along the hit distance starts, as a fraction of SSRMaxDistance. Distance is the length
     * the ray travelled in view space, so this keeps the far end of the range from ending in a hard edge.
     */
    TKDeclareParam(float, SSRRangeFadeStart);
    /**
     * Weight the previous frame keeps in the reflection accumulation. The trace shoots one stochastic
     * sample per pixel, so a rough surface converges over the frames; 0 drops the history and leaves the
     * single sample of this frame in place.
     */
    TKDeclareParam(float, SSRHistoryWeight);
    /**
     * How far the sampled reflection direction is bent towards the mirror direction, 0 leaves the pure
     * GGX lobe and 1 makes every pixel shoot the mirror ray. A bias trades the width of the blur for a
     * lower sample count.
     */
    TKDeclareParam(float, SSRBRDFBias);
    TKDeclareParam(bool, SSRDebugView);
    TKDeclareParam(int, SSRDebugViewMode);

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

    /**
     * Hands the frame rate in these settings to the engine's Timing, which is what actually gates the
     * frame. Every load applies it, because the settings object outlives a load: the editor starts from
     * the user config and then loads the open project's Config/Engine.settings on top.
     */
    void ApplyFrameRate();

   public:
    WindowSettingsPtr m_window;
    GraphicSettingsPtr m_graphics;
    PostProcessingSettingsPtr m_postProcessing;
    ShaderSettingsPtr m_shaderSettings;
    StringSet m_loadedPlugins;
  };

} // namespace ToolKit
