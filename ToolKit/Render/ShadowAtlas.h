/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Types.h"

#include <array>

namespace ToolKit
{

  /**
   * Fixed-layout shadow atlas with 2 layers.
   *
   * Layer 0: 4 Half slots (atlas/2) for cascade shadow maps.
   * Layer 1: 12 Quarter slots (atlas/4) in a regular 4x3 grid (rows 0-2)
   *          + 16 Eighth slots (atlas/8) in 2 rows at the bottom (row 3).
   *
   * The layout is ratio based, so it follows the atlas size (1K or 2K) and a slot scale. Scale 1
   * is the layout above. Scale 2 halves every slot's resolution, which doubles the slot grid on
   * each axis while the atlas keeps its size: 4 Half slots become 16, 12 Quarter become 48 and
   * 16 Eighth become 64. Four slots fit where one used to, so the atlas trades sharpness for the
   * number of lights that can cast a shadow.
   *
   * Slots of a tier sit in a row major pixel grid, because ShadowAtlasLut in shadow.shader
   * rebuilds a slot's coordinate from its index: slot i is at
   * (i % columns, rowOffset + i / columns) * resolution.
   */
  class TK_API ShadowAtlas
  {
   public:
    /** Slot size tiers relative to atlas size. */
    enum class SlotSize
    {
      Half,    //!< atlas / 2 at scale 1 (Layer 0: cascade)
      Quarter, //!< atlas / 4 at scale 1 (Layer 1: point/spot)
      Eighth   //!< atlas / 8 at scale 1 (Layer 1: point/spot)
    };

    /** Information about an allocated slot. */
    struct SlotInfo
    {
      Vec2 coordinate = Vec2(-1.0f); //!< Top-left coordinate in pixels.
      int layer       = -1;          //!< Layer index in the array texture.
      int resolution  = 0;           //!< Slot resolution in pixels.
      int slotIndex   = -1;          //!< Index within its tier, for reserving the same slot again.
    };

    static constexpr int LayerCount     = 2;

    /** A dense layout is at most this many times denser on each axis than the default one. */
    static constexpr int MaxSlotScale   = 2;

    /**
     * Slot count of the densest layout's eighth tier. Every tier keeps this many occupancy entries,
     * which is what one table sized per tier needs: 16, 48 and 64 slots at scale 2.
     */
    static constexpr int MaxEighthSlots = 16 * MaxSlotScale * MaxSlotScale;

    /**
     * Resets all slots to free state and selects the layout density. Must be called each frame
     * before allocation.
     * @param slotScale 1 keeps the default layout, 2 halves every slot's resolution and doubles
     *                  the slot count on each axis. Clamped to [1, MaxSlotScale].
     */
    void Reset(int slotScale);

    /** Slot scale the atlas was last reset with. */
    int GetSlotScale() const;

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

    /**
     * Allocates count slots that are consecutive in the tier's index order, or nothing at all. A point
     * light's cube is sampled by deriving the slot of face 0 and stepping through the faces from there,
     * so its slots have to be one run; a scatter of free slots would sample a neighbour's map.
     */
    bool AllocateContiguous(SlotSize size, int count, int atlasSize, SlotInfo* outSlots);

    /**
     * Allocates count slots that are consecutive in the tier's index order, or nothing at all, and
     * keeps the run inside one grid row unless it starts at a row start.
     *
     * Cascades use this: the shader locates cascade k from cascade 0's coordinate plus the grid
     * offset of k, so a run that starts mid row has to end inside that row. Crossing it would wrap
     * to the next row in the shader while the atlas holds the slots back to back, and the cascade
     * would sample another light's map.
     * @param size The slot size tier to allocate.
     * @param count Number of slots the run needs.
     * @param atlasSize The atlas resolution in pixels.
     * @param outSlots Array to receive allocated slot info. Must have at least count elements.
     * @return true if the run was allocated, false otherwise (no slots consumed on failure).
     */
    bool AllocateRowRun(SlotSize size, int count, int atlasSize, SlotInfo* outSlots);

    /** Returns the resolution in pixels for a given slot size tier and atlas size. */
    int GetSlotResolution(SlotSize size, int atlasSize) const;

    /** Slot count of a tier in the current layout. */
    int GetSlotCount(SlotSize size) const;

    /** True when the indexed slot of that tier is still free. */
    bool IsSlotFree(SlotSize size, int slotIndex) const;

    /** Marks the indexed slot of that tier as used. Fails if it is out of range or already used. */
    bool ReserveSlot(SlotSize size, int slotIndex);

    /** Slot info for a known index, without allocating it. */
    SlotInfo GetSlotInfo(SlotSize size, int slotIndex, int atlasSize) const;

   private:
    /** Number of slot tiers, used to size the per tier occupancy table. */
    static constexpr int TierCount = 3;

    /** Ratio based geometry of a slot tier, in slot units, at slot scale 1. */
    struct TierLayout
    {
      int divisor;   //!< Atlas size divided by this is the slot resolution.
      int rows;      //!< Slot rows the tier occupies.
      int rowOffset; //!< First row of the tier region, in slot units.
    };

    /** Geometry of a tier at slot scale 1. */
    static TierLayout GetTierLayout(SlotSize size);

    /**
     * Shared body of the contiguous allocators. Takes the first free run of count slots, and with
     * @p keepInRow refuses a run that starts mid row and would end in the next one.
     */
    bool AllocateRun(SlotSize size, int count, int atlasSize, SlotInfo* outSlots, bool keepInRow);

    /** Slot columns of a tier in the current layout. Also the atlas size over the slot resolution. */
    int GetColumnCount(SlotSize size) const;

    /** Slot rows of a tier in the current layout. */
    int GetRowCount(SlotSize size) const;

    /** First row of a tier's region in the current layout, in slot units. */
    int GetRowOffset(SlotSize size) const;

    /** Occupancy table of a tier. */
    bool* GetSlotTable(SlotSize size);

    /** Occupancy table of a tier, read only. */
    const bool* GetSlotTable(SlotSize size) const;

    int m_slotScale = 1;

    /** Tier major occupancy. Every tier keeps MaxEighthSlots entries, so one table holds them all. */
    std::array<std::array<bool, MaxEighthSlots>, TierCount> m_slots {};
  };

} // namespace ToolKit
