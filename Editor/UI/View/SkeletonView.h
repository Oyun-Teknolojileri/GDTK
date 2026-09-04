/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "EditorTypes.h"
#include "View.h"

#include <Skeleton.h>

namespace ToolKit
{
  namespace Editor
  {

    // SkeletonView
    //////////////////////////////////////////

    class TK_EDITOR_API SkeletonView : public View
    {
     public:
      SkeletonView();
      virtual ~SkeletonView();

      void Show() override;
      void SetSkeleton(SkeletonPtr skeleton);

     private:
      void ShowBone(const DynamicBoneMap::DynamicBone* dBone);

     public:
      SkeletonPtr m_skeleton = nullptr;
      bool m_isTempView      = false;
    };

    // SkeletonWindow
    //////////////////////////////////////////

    class TK_EDITOR_API SkeletonWindow : public Window
    {
     public:
      TKDeclareClass(SkeletonWindow, Window);

      SkeletonWindow();
      virtual ~SkeletonWindow();

      void SetSkeleton(SkeletonPtr skeleton);
      void Show() override;

     private:
      SkeletonViewPtr m_view;
    };

  } // namespace Editor
} // namespace ToolKit
