/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Pass.h"

namespace ToolKit
{

  struct BillboardPassParams
  {
    Viewport* Viewport = nullptr;
    EntityPtrArray Billboards;
  };

  class TK_API BillboardPass : public Pass
  {
   public:
    BillboardPass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

   public:
    BillboardPassParams m_params;
    EntityPtrArray m_noDepthBillboards;

   private:
    RenderData m_renderData;

    /** Pass-owned passive RenderState. depthTestEnabled is refreshed before each draw to
     *  match the param-driven depth-tested vs bypass-depth-test billboard groups. */
    RenderState m_passState;
  };

  typedef std::shared_ptr<BillboardPass> BillboardPassPtr;

} // namespace ToolKit