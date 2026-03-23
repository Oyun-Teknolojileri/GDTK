/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "ShadowAtlas.h"

#include <cassert>
#include <cstring>

#include "DebugNew.h"

namespace ToolKit
{

  void ShadowAtlas::Reset()
  {
    memset(m_halfSlots, 0, sizeof(m_halfSlots));
    memset(m_quarterSlots, 0, sizeof(m_quarterSlots));
    memset(m_eighthSlots, 0, sizeof(m_eighthSlots));
  }

  int ShadowAtlas::GetSlotResolution(SlotSize size, int atlasSize)
  {
    switch (size)
    {
      case SlotSize::Half:
        return atlasSize / 2;
      case SlotSize::Quarter:
        return atlasSize / 4;
      case SlotSize::Eighth:
        return atlasSize / 8;
      default:
        assert(false && "Unknown slot size");
        return 0;
    }
  }

  ShadowAtlas::SlotInfo ShadowAtlas::Allocate(SlotSize size, int atlasSize)
  {
    SlotInfo info;

    if (size == SlotSize::Half)
    {
      // Layer 0: 4 Half slots in a 2x2 grid.
      // +-------+-------+
      // |  H0   |  H1   |
      // +-------+-------+
      // |  H2   |  H3   |
      // +-------+-------+
      int slotRes = atlasSize / 2;
      for (int i = 0; i < HalfSlotCount; i++)
      {
        if (!m_halfSlots[i])
        {
          m_halfSlots[i]  = true;
          int col         = i % 2;
          int row         = i / 2;
          info.coordinate = Vec2((float) (col * slotRes), (float) (row * slotRes));
          info.layer      = 0;
          info.resolution = slotRes;
          return info;
        }
      }
    }
    else if (size == SlotSize::Quarter)
    {
      // Layer 1: 12 Quarter slots in a regular 4x3 grid (rows 0-2).
      //
      // +----+----+----+----+
      // | Q0 | Q1 | Q2 | Q3 |
      // +----+----+----+----+
      // | Q4 | Q5 | Q6 | Q7 |
      // +----+----+----+----+
      // | Q8 | Q9 |Q10 |Q11 |
      // +----+----+----+----+
      // |   Eighth slots    |
      // +-------------------+
      int slotRes                                  = atlasSize / 4;

      static const int coords[QuarterSlotCount][2] = {
          {0, 0},
          {1, 0},
          {2, 0},
          {3, 0}, // row 0
          {0, 1},
          {1, 1},
          {2, 1},
          {3, 1}, // row 1
          {0, 2},
          {1, 2},
          {2, 2},
          {3, 2}  // row 2
      };

      for (int i = 0; i < QuarterSlotCount; i++)
      {
        if (!m_quarterSlots[i])
        {
          m_quarterSlots[i] = true;
          info.coordinate   = Vec2((float) (coords[i][0] * slotRes), (float) (coords[i][1] * slotRes));
          info.layer        = 1;
          info.resolution   = slotRes;
          return info;
        }
      }
    }
    else if (size == SlotSize::Eighth)
    {
      // Layer 1: 16 Eighth slots in the bottom row (row 3 of quarter-grid = rows 6-7 of eighth-grid).
      // 8 slots per eighth-row, 2 eighth-rows fit in one quarter-row.
      //
      // +--+--+--+--+--+--+--+--+
      // |E0|E1|E2|E3|E4|E5|E6|E7|
      // +--+--+--+--+--+--+--+--+
      // |E8|E9|EA|EB|EC|ED|EE|EF|
      // +--+--+--+--+--+--+--+--+
      int slotRes = atlasSize / 8;

      for (int i = 0; i < EighthSlotCount; i++)
      {
        if (!m_eighthSlots[i])
        {
          m_eighthSlots[i] = true;
          int col          = i % 8;
          int row          = 6 + (i / 8);
          info.coordinate  = Vec2((float) (col * slotRes), (float) (row * slotRes));
          info.layer       = 1;
          info.resolution  = slotRes;
          return info;
        }
      }
    }

    // Allocation failed.
    return info;
  }

  bool ShadowAtlas::AllocateN(SlotSize size, int count, int atlasSize, SlotInfo* outSlots)
  {
    // Quick check: enough free slots before attempting allocation.
    int free = 0;
    if (size == SlotSize::Quarter)
    {
      for (int i = 0; i < QuarterSlotCount; i++)
      {
        if (!m_quarterSlots[i])
          free++;
      }
    }
    else if (size == SlotSize::Eighth)
    {
      for (int i = 0; i < EighthSlotCount; i++)
      {
        if (!m_eighthSlots[i])
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
