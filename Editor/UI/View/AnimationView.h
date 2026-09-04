/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "EditorTypes.h"
#include "View.h"

namespace ToolKit
{
  namespace Editor
  {

    // AnimationView
    //////////////////////////////////////////

    class TK_EDITOR_API AnimationView : public View
    {
     public:
      AnimationView();
      virtual ~AnimationView();

      void Show() override;
      void SetAnimation(AnimationPtr anim);

     public:
      AnimationPtr m_animation = nullptr;
      char m_searchBuf[128]    = {}; //!< Key name filter, case insensitive.
      bool m_isTempView        = false;
    };

    // AnimationWindow
    //////////////////////////////////////////

    class TK_EDITOR_API AnimationWindow : public Window
    {
     public:
      TKDeclareClass(AnimationWindow, Window);

      AnimationWindow();
      virtual ~AnimationWindow();

      void SetAnimation(AnimationPtr anim);
      void Show() override;

     private:
      AnimationViewPtr m_view;
    };

  } // namespace Editor
} // namespace ToolKit
