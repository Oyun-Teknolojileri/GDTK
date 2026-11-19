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

   protected:
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;

   private:
    void UpdateBoundingBoxCache();

   public:
    TKDeclareParam(Vec3, Size);
    TKDeclareParam(Vec3, PositionOffset);

    /** Bake file, relative to the resource root. Empty means not baked. */
    TKDeclareParam(String, BakeFile);

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
