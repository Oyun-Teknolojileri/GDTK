/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file LightProbeVolumeComponent.h
 *
 * Owns one baked probe volume and the textures the forward pass samples.
 *
 * The component is data and upload only: the bake itself lives in `Modules/LightProbeBake`, and
 * a component that has never been baked (no file, or a file that fails to load) contributes
 * nothing, so a scene without a bake renders exactly as it did before the feature existed.
 */

#include "Component.h"
#include "ProbeVolume.h"

namespace ToolKit
{
  typedef std::shared_ptr<class LightProbeVolumeComponent> LightProbeVolumeComponentPtr;
  typedef std::weak_ptr<class LightProbeVolumeComponent> LightProbeVolumeComponentWeakPtr;
  typedef std::vector<LightProbeVolumeComponentPtr> LightProbeVolumeComponentPtrArray;

  static VariantCategory LightProbeVolumeCategory {"Light Probe Volume", 91};

  class TK_API LightProbeVolumeComponent : public Component
  {
   public:
    TKDeclareClass(LightProbeVolumeComponent, Component);

    LightProbeVolumeComponent();
    ~LightProbeVolumeComponent() override;

    ComponentPtr Copy(EntityPtr ntt) override;

    void Init(bool flushClientSideArray);
    void UnInit();

    /**
     * Called once per frame for every volume in the scene's cache, next to Init.
     *
     * Nothing in the engine needs it - the volume is data and upload only - but a host that runs
     * work around the volume (the editor's bake) needs a per frame tick, and this is the one the
     * scene already provides without a task that has to re-schedule itself.
     */
    virtual void Update();

    const BoundingBox& GetBoundingBox();
    void InvalidateSpatialCaches();

    /** Reads `BakeFile` into m_data. Safe to call repeatedly; a missing file clears the bake. */
    bool LoadBake();
    void UnloadBake();

    /** True when the component holds coefficient data that can be uploaded. */
    bool IsBaked() const { return !m_data.coefficients.empty(); }

    /**
     * Creates or refreshes the three shader textures from the L1 subset of the bake.
     * Has to run on the render thread, because it creates GPU textures.
     * @return False when there is nothing to upload.
     */
    bool EnsureShaderTextures();

    const ProbeVolumeData& GetProbeData() const { return m_data; }

    /** The three slice stacked coefficient textures, or null before EnsureShaderTextures. */
    DataTexturePtr GetShaderTexture(int index) const;

    /** Probe grid a bake would produce for the current box and spacing. */
    UVec3 ComputeBakeResolution() const;

    /**
     * Bake file relative to the resource root, including the default it would be baked to.
     * an empty BakeFile resolves to `LightProbes/<scene name>.pgi`, which is what the editor's
     * Bake button writes and the pack step then finds.
     */
    String ResolveBakeFile();

    /**
     * Resizes the volume to the scene bounds, padded the way the ProbeBake tool pads when it is
     * given no box: 2% of the largest extent, at least 10 cm. The box is written in local space,
     * so an offset or rotated node keeps its own frame.
     */
    void FitToScene();

    // The bake itself lives in `Modules/LightProbeBake`, which the engine does not depend on, so
    // the volume only offers the hooks and a host that links the baker (the editor) overrides
    // them. A game answers "not baking" and the bake buttons do nothing.
    //////////////////////////////////////////

    /** Starts a bake. The engine base never bakes. */
    virtual void RequestBake() {}

    /** Asks a running bake to stop. */
    virtual void RequestCancelBake() {}

    virtual bool IsBaking() const { return false; }

    /** Fractions of the probes traced, 0 to 1. */
    virtual float GetBakeProgress() const { return 0.0f; }

    /** One line the inspector shows under the bake button. */
    virtual String GetBakeStatus() const { return "Not baked."; }

   protected:
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;

   private:
    void UpdateBoundingBoxCache();

    /**
     * Fits a world space box into the owner node's local space.
     *
     * Size and PositionOffset are local, while a bake traces and stores a world space box, so every
     * place that moves a box between the two goes through here.
     */
    BoundingBox WorldBoxToLocal(const BoundingBox& worldBox);

   public:
    TKDeclareParam(Vec3, Size);
    TKDeclareParam(Vec3, PositionOffset);

    /** Bake file, relative to the resource root. Empty means not baked. */
    TKDeclareParam(String, BakeFile);

    /** Distance between neighbouring probes in world units; the probe grid follows from it. */
    TKDeclareParam(float, ProbeSpacing);

    /** Paths traced per probe. More is smoother and slower. */
    TKDeclareParam(int, BakeSamples);

    /** Surface bounces per path. */
    TKDeclareParam(int, BakeBounces);

    /** Sampling seed, so the same settings reproduce the same bake. */
    TKDeclareParam(int, BakeSeed);

    /** Fits Size and PositionOffset to the scene bounds. Also on the LightProbe entity. */
    TKDeclareParam(VariantCallback, FitToScene);

    TKDeclareParam(float, Intensity);
    TKDeclareParam(float, AmbientOcclusionStrength);

    bool m_spatialCachesInvalidated = true;

   private:
    BoundingBox m_boundingBoxCache;
    ProbeVolumeData m_data;

    /** Channel major L1 storage: texture k holds component k of coefficients 0..3. */
    DataTexturePtr m_shTextures[3];
    bool m_texturesValid = false;
    bool m_initialized   = false;
  };

} // namespace ToolKit
