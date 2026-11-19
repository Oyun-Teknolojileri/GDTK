/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file Sh.h
 *
 * Real spherical harmonics, second order, in the basis order the shader evaluates:
 * (Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22).
 *
 * The bake side stores `E / pi`, i.e. the radiance coefficients already convolved with the
 * clamped cosine (`A_l / pi` = 1, 2/3, 1/4) so the shader can multiply albedo by the result
 * without a constant of its own.
 */

#include "BakeTypes.h"

namespace ToolKit
{
  namespace LightProbeBake
  {

    constexpr int ShCoefficientCount = 9;

    /** A_l / pi for the three bands. Applied once, at the end of the projection. */
    constexpr float ShBandFactor[ShCoefficientCount] = {1.0f,
                                                        0.6666667f,
                                                        0.6666667f,
                                                        0.6666667f,
                                                        0.25f,
                                                        0.25f,
                                                        0.25f,
                                                        0.25f,
                                                        0.25f};

    struct Sh9
    {
      Vec3 c[ShCoefficientCount];
    };

    inline void ShBasis(const Vec3& d, float out[ShCoefficientCount])
    {
      const float x = d.x;
      const float y = d.y;
      const float z = d.z;

      out[0] = 0.282095f;
      out[1] = 0.488603f * y;
      out[2] = 0.488603f * z;
      out[3] = 0.488603f * x;
      out[4] = 1.092548f * x * y;
      out[5] = 1.092548f * y * z;
      out[6] = 0.315392f * (3.0f * z * z - 1.0f);
      out[7] = 1.092548f * x * z;
      out[8] = 0.546274f * (x * x - y * y);
    }

    /** Evaluates E / pi for a surface normal. The shader runs the same expression. */
    inline Vec3 ShEvaluate(const Sh9& sh, const Vec3& normal)
    {
      float basis[ShCoefficientCount];
      ShBasis(normal, basis);

      Vec3 result(0.0f);
      for (int i = 0; i < ShCoefficientCount; ++i)
      {
        result += sh.c[i] * basis[i];
      }

      return result;
    }

  } // namespace LightProbeBake
} // namespace ToolKit
