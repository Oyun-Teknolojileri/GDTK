/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "SpriteSheet.h"
#include "Surface.h"
#include "Types.h"

namespace ToolKit
{

  static VariantCategory CanvasCategory {"Canvas", 90};

  /**
   * A logical entity used as a storage for surfaces.
   * Used to resize and transform the containing Surfaces for responsible user interfaces.
   */
  class TK_API Canvas : public Surface
  {
   public:
    TKDeclareClass(Canvas, Surface);

    Canvas();
    void NativeConstruct() override;
    void ApplyRecursiveResizePolicy(float width, float height);

    /** Skip geometry creation. Canvas does not uses a mesh. */
    void UpdateGeometry(bool byTexture) override;

   protected:
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;

    virtual void DeserializeComponents(const SerializationFileInfo& info, XmlNode* entityNode);
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;
    XmlNode* DeSerializeImpV045(const SerializationFileInfo& info, XmlNode* parent);

    /** Overrides the quad creation to skip mesh generation. Canvas does not need a mesh. */
    void CreateQuat() override {};

   public:
    // Local events.
    SurfaceEventCallback m_onMouseEnterLocal;
    SurfaceEventCallback m_onMouseExitLocal;
  };

} //  namespace ToolKit
