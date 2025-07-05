/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Renderer.h"
#include "Sky.h"

namespace ToolKit
{

  class TK_API GradientSky : public SkyBase
  {
   public:
    TKDeclareClass(GradientSky, SkyBase);

    GradientSky();
    virtual ~GradientSky();

    void Init() override;
    MaterialPtr GetSkyboxMaterial() override;

    bool IsReadyToRender() override;

   protected:
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;
    void GenerateGradientCubemap(Renderer* renderer);
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;

   private:
    /** Checks if the sky has default values. */
    bool IsDefault();

   public:
    TKDeclareParam(Vec3, TopColor);
    TKDeclareParam(Vec3, MiddleColor);
    TKDeclareParam(Vec3, BottomColor);
    TKDeclareParam(float, GradientExponent);
    TKDeclareParam(VariantCallback, ReGenerateIrradianceMap);

   private:
    bool m_waitingForInit  = false;
    const int m_skyMapSize = 512;
  };

} // namespace ToolKit