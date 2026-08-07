/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Types.h"

namespace ToolKit
{

  /**
   * Fixed-layout shadow atlas with 2 layers.
   *
   * Layer 0: 4 Half slots (atlas/2) for cascade shadow maps.
   * Layer 1: 12 Quarter slots (atlas/4) in a regular 4x3 grid (rows 0-2)
   *          + 16 Eighth slots (atlas/8) in 2 rows at the bottom (row 3).
   *
   * Layout is ratio-based, independent of atlas size (1K or 2K).
   */
  class TK_API ShadowAtlas
  {
   public:
    /** Slot size tiers relative to atlas size. */
    enum class SlotSize
    {
      Half,    //!< atlas / 2  (Layer 0: cascade)
      Quarter, //!< atlas / 4  (Layer 1: point/spot)
      Eighth   //!< atlas / 8  (Layer 1: point/spot)
    };

    /** Information about an allocated slot. */
    struct SlotInfo
    {
      Vec2 coordinate = Vec2(-1.0f); //!< Top-left coordinate in pixels.
      int layer       = -1;          //!< Layer index in the array texture.
      int resolution  = 0;           //!< Slot resolution in pixels.
    };

    static constexpr int LayerCount       = 2;

    static constexpr int HalfSlotCount    = 4;
    static constexpr int QuarterSlotCount = 12;
    static constexpr int EighthSlotCount  = 16;

    /** Resets all slots to free state. Must be called each frame before allocation. */
    void Reset();

    /**
     * Allocates a slot of the given size tier.
     * @param size The slot size tier to allocate.
     * @param atlasSize The atlas resolution in pixels (1024 or 2048).
     * @return Slot info with coordinate, layer and resolution. Layer will be -1 if allocation fails.
     */
    SlotInfo Allocate(SlotSize size, int atlasSize);

    /**
     * Attempts to allocate count slots of the given size tier atomically.
     * Either all slots are allocated or none are.
     * @param size The slot size tier to allocate.
     * @param count Number of slots to allocate.
     * @param atlasSize The atlas resolution in pixels.
     * @param outSlots Array to receive allocated slot info. Must have at least count elements.
     * @return true if all slots were allocated, false otherwise (no slots consumed on failure).
     */
    bool AllocateN(SlotSize size, int count, int atlasSize, SlotInfo* outSlots);

    /** Returns the resolution in pixels for a given slot size tier and atlas size. */
    static int GetSlotResolution(SlotSize size, int atlasSize);

   private:
    bool m_halfSlots[HalfSlotCount]       = {};
    bool m_quarterSlots[QuarterSlotCount] = {};
    bool m_eighthSlots[EighthSlotCount]   = {};
  };

} // namespace ToolKit
