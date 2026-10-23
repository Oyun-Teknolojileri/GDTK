/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "ShadowAtlas.h"

#include <cassert>

#include "DebugNew.h"

namespace ToolKit
{

  namespace
  {

    //! Atlas size over these is the slot resolution at slot scale 1.
    constexpr int HalfDivisor     = 2;
    constexpr int QuarterDivisor  = 4;
    constexpr int EighthDivisor   = 8;

    //! Slot rows each tier occupies at slot scale 1.
    constexpr int HalfRows        = 2; //!< Layer 0 is a 2x2 grid of half slots.
    constexpr int QuarterRows     = 3; //!< Rows 0-2 of layer 1.
    constexpr int EighthRows      = 2; //!< Bottom quarter row of layer 1, two eighth rows.

    //! First row of the eighth slots, in eighth slot units: the three quarter rows above them.
    constexpr int EighthRowOffset = 6;

  } // namespace

  void ShadowAtlas::Reset(int slotScale)
  {
    m_slotScale = slotScale < 1 ? 1 : (slotScale > MaxSlotScale ? MaxSlotScale : slotScale);
    m_slots     = {};
  }

  int ShadowAtlas::GetSlotScale() const { return m_slotScale; }

  ShadowAtlas::TierLayout ShadowAtlas::GetTierLayout(SlotSize size)
  {
    switch (size)
    {
      case SlotSize::Half:
        return {HalfDivisor, HalfRows, 0};
      case SlotSize::Quarter:
        return {QuarterDivisor, QuarterRows, 0};
      case SlotSize::Eighth:
        return {EighthDivisor, EighthRows, EighthRowOffset};
      default:
        assert(false && "Unknown slot size");
        return {HalfDivisor, HalfRows, 0};
    }
  }

  int ShadowAtlas::GetColumnCount(SlotSize size) const { return GetTierLayout(size).divisor * m_slotScale; }

  int ShadowAtlas::GetRowCount(SlotSize size) const { return GetTierLayout(size).rows * m_slotScale; }

  int ShadowAtlas::GetRowOffset(SlotSize size) const { return GetTierLayout(size).rowOffset * m_slotScale; }

  bool* ShadowAtlas::GetSlotTable(SlotSize size)
  {
    const int tier = static_cast<int>(size);
    assert(tier >= 0 && tier < TierCount && "Unknown slot size");
    return m_slots[tier].data();
  }

  const bool* ShadowAtlas::GetSlotTable(SlotSize size) const
  {
    const int tier = static_cast<int>(size);
    assert(tier >= 0 && tier < TierCount && "Unknown slot size");
    return m_slots[tier].data();
  }

  int ShadowAtlas::GetSlotCount(SlotSize size) const { return GetColumnCount(size) * GetRowCount(size); }

  int ShadowAtlas::GetSlotResolution(SlotSize size, int atlasSize) const { return atlasSize / GetColumnCount(size); }

  bool ShadowAtlas::IsSlotFree(SlotSize size, int slotIndex) const
  {
    return slotIndex >= 0 && slotIndex < GetSlotCount(size) && !GetSlotTable(size)[slotIndex];
  }

  bool ShadowAtlas::ReserveSlot(SlotSize size, int slotIndex)
  {
    if (!IsSlotFree(size, slotIndex))
    {
      return false;
    }

    GetSlotTable(size)[slotIndex] = true;
    return true;
  }

  ShadowAtlas::SlotInfo ShadowAtlas::GetSlotInfo(SlotSize size, int slotIndex, int atlasSize) const
  {
    SlotInfo info;
    if (slotIndex < 0 || slotIndex >= GetSlotCount(size))
    {
      return info;
    }

    const int columns = GetColumnCount(size);

    info.resolution   = GetSlotResolution(size, atlasSize);
    info.slotIndex    = slotIndex;
    // Cascades share layer 0, the local light tiers share layer 1.
    info.layer        = size == SlotSize::Half ? 0 : 1;
    info.coordinate   = Vec2((float) ((slotIndex % columns) * info.resolution),
                             (float) ((GetRowOffset(size) + slotIndex / columns) * info.resolution));

    return info;
  }

  ShadowAtlas::SlotInfo ShadowAtlas::Allocate(SlotSize size, int atlasSize)
  {
    // Layer 0: 2x2 Half slots for cascades, 4x4 in a dense layout. Layer 1 at scale 1:
    //
    //   +----+----+----+----+
    //   | Q0 | Q1 | Q2 | Q3 |    rows 0-2 of the atlas in quarter slots
    //   +----+----+----+----+
    //   | Q4 | Q5 | Q6 | Q7 |
    //   +----+----+----+----+
    //   | Q8 | Q9 |Q10 |Q11 |
    //   +--+--+--+--+--+--+--+--+
    //   |E0|E1|E2|E3|E4|E5|E6|E7|    bottom quarter row in eighth slots
    //   +--+--+--+--+--+--+--+--+
    //   |E8|E9|EA|EB|EC|ED|EE|EF|
    //   +--+--+--+--+--+--+--+--+
    //
    // A dense layout halves the slot size, so every grid above holds twice as many slots per axis.
    const int slotCount = GetSlotCount(size);
    for (int i = 0; i < slotCount; i++)
    {
      if (IsSlotFree(size, i))
      {
        ReserveSlot(size, i);
        return GetSlotInfo(size, i, atlasSize);
      }
    }

    // Allocation failed.
    return SlotInfo();
  }

  bool ShadowAtlas::AllocateRun(SlotSize size, int count, int atlasSize, SlotInfo* outSlots, bool keepInRow)
  {
    const int slotCount = GetSlotCount(size);
    if (count <= 0 || count > slotCount)
    {
      return false;
    }

    const int columns = GetColumnCount(size);

    for (int start = 0; start + count <= slotCount; start++)
    {
      // A run that starts mid row has to end inside that row, because the shader steps the grid
      // from the first slot of the run. A run at a row start may span rows, the step then wraps
      // exactly the way the atlas stores the slots.
      const int column = start % columns;
      if (keepInRow && column != 0 && column + count > columns)
      {
        continue;
      }

      bool free = true;
      for (int i = 0; i < count && free; i++)
      {
        free = IsSlotFree(size, start + i);
      }

      if (!free)
      {
        continue;
      }

      for (int i = 0; i < count; i++)
      {
        ReserveSlot(size, start + i);
        outSlots[i] = GetSlotInfo(size, start + i, atlasSize);
      }

      return true;
    }

    return false;
  }

  bool ShadowAtlas::AllocateContiguous(SlotSize size, int count, int atlasSize, SlotInfo* outSlots)
  {
    return AllocateRun(size, count, atlasSize, outSlots, false);
  }

  bool ShadowAtlas::AllocateRowRun(SlotSize size, int count, int atlasSize, SlotInfo* outSlots)
  {
    return AllocateRun(size, count, atlasSize, outSlots, true);
  }

  bool ShadowAtlas::AllocateN(SlotSize size, int count, int atlasSize, SlotInfo* outSlots)
  {
    const int slotCount = GetSlotCount(size);
    if (count <= 0 || count > slotCount)
    {
      return false;
    }

    // Quick check: enough free slots before attempting allocation.
    const bool* table = GetSlotTable(size);
    int free          = 0;
    for (int i = 0; i < slotCount && free < count; i++)
    {
      if (!table[i])
      {
        free++;
      }
    }

    if (free < count)
    {
      return false;
    }

    // Pre-check passed, allocate all. Should not fail.
    for (int i = 0; i < count; i++)
    {
      outSlots[i] = Allocate(size, atlasSize);
      assert(outSlots[i].layer >= 0);
    }

    return true;
  }

} // namespace ToolKit
