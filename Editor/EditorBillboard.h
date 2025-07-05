/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorTypes.h"

namespace ToolKit
{
  namespace Editor
  {

    // EditorBillboardBase
    //////////////////////////////////////////

    class TK_EDITOR_API EditorBillboardBase : public Billboard
    {
     public:
      enum class BillboardType
      {
        Cursor,
        Axis3d,
        Gizmo,
        Move,
        Rotate,
        Scale,
        Sky,
        Light,
        Anchor
      };

     public:
      TKDeclareClass(EditorBillboardBase, Billboard);

      EditorBillboardBase();
      explicit EditorBillboardBase(const Settings& settings);
      virtual BillboardType GetBillboardType() const = 0;
      void NativeConstruct() override;

     protected:
      virtual void Generate();

     protected:
      TexturePtr m_iconImage = nullptr;
    };

    // SkyBillboard
    //////////////////////////////////////////

    class TK_EDITOR_API SkyBillboard : public EditorBillboardBase
    {
     public:
      TKDeclareClass(SkyBillboard, EditorBillboardBase);

      SkyBillboard();
      virtual ~SkyBillboard();
      BillboardType GetBillboardType() const override;
      void LookAt(CameraPtr cam, float scale) override;

     private:
      void Generate() override;
    };

    // LightBillboard
    //////////////////////////////////////////

    class TK_EDITOR_API LightBillboard : public EditorBillboardBase
    {
     public:
      TKDeclareClass(LightBillboard, EditorBillboardBase);

      LightBillboard();
      virtual ~LightBillboard();
      BillboardType GetBillboardType() const override;
      void LookAt(CameraPtr cam, float scale) override;

     private:
      void Generate() override;
    };

  } // namespace Editor
} // namespace ToolKit