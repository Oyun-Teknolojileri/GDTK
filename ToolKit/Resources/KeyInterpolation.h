/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file KeyInterpolation.h Interpolation modes of a key and the sampling math shared by
 * Animation and the standalone interpolation check program.
 *
 * Nothing here depends on the engine: glm values, plain scalars and the standard library
 * only. That is deliberate, it lets the math be compiled and asserted on its own, without
 * a window, a backend or a scene (see animation-interpolation-plan.md, section 8).
 *
 * glm's storage options are expected to be set before this header is reached. The engine
 * does it in Types.h / stdafx.h; a standalone build has to do the same.
 */

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ToolKit
{
  /**
   * How a key takes part in the interpolation of the segments around it.
   *
   * The shape of a segment is decided by both of its endpoints and the hard modes win:
   * Stepped beats Linear, Linear beats the cubic modes. See ResolveSegment.
   */
  enum class KeyInterp : std::uint8_t
  {
    Stepped = 0, //!< Hold this key's value until the next key, then snap.
    Linear  = 1, //!< Straight line / slerp. The behaviour before modes existed, and the default.
    Smooth  = 2, //!< Cubic, with the tangent derived from the neighbouring keys.
    Flat    = 3  //!< Cubic, with a zero tangent here: a settle, or a soft start / stop.
  };

  /** Shape a segment takes once both of its endpoints have been resolved. */
  enum class SegmentKind : std::uint8_t
  {
    Hold,     //!< Keep the first key's value for the whole segment.
    Straight, //!< Straight line (position, scale) or plain slerp (rotation).
    Curve     //!< Cubic through both keys.
  };

  namespace Interpolation
  {
    /**
     * A slope whose magnitude reaches this leaves the monotonicity region of a single cubic
     * interval: measured in units of the segment's secant, a cubic whose two end slopes are both
     * within this bound cannot leave the value range of its endpoints.
     */
    constexpr float MaxCubicSlope = 3.0f;

    /**
     * Resolves the shape of one segment from the modes of its two endpoints. The hard modes
     * win, so a Flat key next to a Linear one stays on the straight line instead of
     * producing the velocity jump that Flat exists to remove.
     */
    inline SegmentKind ResolveSegment(KeyInterp start, KeyInterp end)
    {
      if (start == KeyInterp::Stepped || end == KeyInterp::Stepped)
      {
        return SegmentKind::Hold;
      }

      if (start == KeyInterp::Linear || end == KeyInterp::Linear)
      {
        return SegmentKind::Straight;
      }

      return SegmentKind::Curve;
    }

    // Cubic Hermite basis
    //////////////////////////////////////////

    inline float Hermite00(float t) { return ((2.0f * t) - 3.0f) * t * t + 1.0f; }
    inline float Hermite10(float t) { return ((t - 2.0f) * t + 1.0f) * t; }
    inline float Hermite01(float t) { return (3.0f - (2.0f * t)) * t * t; }
    inline float Hermite11(float t) { return (t - 1.0f) * t * t; }

    // Slopes and tangents
    //////////////////////////////////////////

    /** Value change of a segment per second. */
    inline float Secant(float start, float end, float duration)
    {
      return duration > 0.0f ? (end - start) / duration : 0.0f;
    }

    /** Value change of a segment per second, per component. */
    inline glm::vec3 Secant(const glm::vec3& start, const glm::vec3& end, float duration)
    {
      return duration > 0.0f ? (end - start) / duration : glm::vec3(0.0f);
    }

    /**
     * Keeps a key's slope inside the monotonicity region of both segments it takes part in.
     *
     * The limit comes from the smaller of the two secants, and it is applied to the key, not to a
     * segment: the one tangent a key carries then stays valid on either side of it, which is what
     * keeps the velocity continuous at the key while still ruling out overshoot. Limiting the two
     * tangents of a segment separately would let one side clamp and the other not, and the velocity
     * would jump exactly where the mode exists to smooth it.
     */
    inline float LimitSlope(float slope, float previousSecant, float nextSecant)
    {
      const float limit = MaxCubicSlope * std::min(std::fabs(previousSecant), std::fabs(nextSecant));
      return glm::clamp(slope, -limit, limit);
    }

    /**
     * Auto tangent of an interior key: the average of the two neighbouring secants, limited by
     * LimitSlope, flattened to zero where the motion turns around. That flattening is what keeps
     * the curve inside the key values (the behaviour of Maya Auto, Blender Auto Clamped, Unity
     * Clamped Auto).
     */
    inline float AutoTangent(float previous, float current, float next, float previousDuration, float nextDuration)
    {
      const float incoming = Secant(previous, current, previousDuration);
      const float outgoing = Secant(current, next, nextDuration);

      if (incoming * outgoing <= 0.0f)
      {
        return 0.0f; // Local extremum, or a flat stretch: no speed at the key.
      }

      return LimitSlope((incoming + outgoing) * 0.5f, incoming, outgoing);
    }

    inline glm::vec3 AutoTangent(const glm::vec3& previous,
                                 const glm::vec3& current,
                                 const glm::vec3& next,
                                 float previousDuration,
                                 float nextDuration)
    {
      const glm::vec3 incoming = Secant(previous, current, previousDuration);
      const glm::vec3 outgoing = Secant(current, next, nextDuration);

      glm::vec3 tangent(0.0f);

      // Per component, the way a DCC clamps per channel: an axis that peaks here must not drag
      // along the axes that are still moving.
      for (int i = 0; i < 3; i++)
      {
        if (incoming[i] * outgoing[i] > 0.0f)
        {
          tangent[i] = LimitSlope((incoming[i] + outgoing[i]) * 0.5f, incoming[i], outgoing[i]);
        }
      }

      return tangent;
    }

    // Rotation
    //////////////////////////////////////////

    /**
     * Rotation vector that takes `from` to `to`, in world space: the axis times the angle, so its
     * length is the arc speed of that step and its direction is the axis the rotation turns around.
     * That direction is the quantity to measure when a rotation has to be checked for a whip at a
     * key: the speed can stay continuous while this vector jumps.
     */
    inline glm::vec3 QuatRotationVector(const glm::quat& from, const glm::quat& to)
    {
      glm::quat relative = to * glm::inverse(from);

      if (relative.w < 0.0f)
      {
        relative = -relative;
      }

      const glm::vec3 imaginary(relative.x, relative.y, relative.z);
      const float magnitude = glm::length(imaginary);

      if (magnitude < 0.0000001f)
      {
        return glm::vec3(0.0f);
      }

      return imaginary * (2.0f * std::atan2(magnitude, relative.w) / magnitude);
    }

    // Evaluation
    //////////////////////////////////////////

    /** Evaluates the cubic Hermite of one component. Tangents are per second, duration too. */
    inline float
    HermiteValue(float start, float end, float startTangent, float endTangent, float duration, float tau)
    {
      return (Hermite00(tau) * start) + (Hermite10(tau) * duration * startTangent) + (Hermite01(tau) * end) +
             (Hermite11(tau) * duration * endTangent);
    }

    inline glm::vec3 HermiteValue(const glm::vec3& start,
                                  const glm::vec3& end,
                                  const glm::vec3& startTangent,
                                  const glm::vec3& endTangent,
                                  float duration,
                                  float tau)
    {
      return (Hermite00(tau) * start) + (Hermite10(tau) * duration * startTangent) + (Hermite01(tau) * end) +
             (Hermite11(tau) * duration * endTangent);
    }

    /** The two keys a sample time falls between, plus the position inside that segment. */
    struct Bracket
    {
      int m_start      = -1;   //!< Key the segment starts at, -1 when the track is empty.
      int m_end        = -1;   //!< Key the segment ends at, equal to m_start for a single key.
      float m_tau      = 0.0f; //!< Position inside the segment, in [0, 1].
      float m_duration = 0.0f; //!< Segment length in seconds.
    };

    /**
     * Finds the bracketing keys of a time, keeping the boundary behaviour the engine has always
     * had: a single key track reports that key twice, a time before the first key reports the
     * first segment at tau 0 and a time after the last key the last segment at tau 1, so a
     * sample never extrapolates.
     *
     * @param count Number of keys.
     * @param time Sample time in seconds.
     * @param timeAt Callable: key index -> key time in seconds, strictly ascending.
     * @return The bracket, with m_start == -1 when there is nothing to sample.
     */
    template <typename TimeFn>
    Bracket FindBracket(int count, float time, TimeFn timeAt)
    {
      Bracket bracket;

      if (count <= 0)
      {
        return bracket;
      }

      if (count == 1)
      {
        bracket.m_start = 0;
        bracket.m_end   = 0;
        return bracket;
      }

      if (time < timeAt(0))
      {
        bracket.m_start    = 0;
        bracket.m_end      = 1;
        bracket.m_duration = timeAt(1) - timeAt(0);
        return bracket;
      }

      if (time > timeAt(count - 1))
      {
        bracket.m_start    = count - 2;
        bracket.m_end      = count - 1;
        bracket.m_duration = timeAt(count - 1) - timeAt(count - 2);
        bracket.m_tau      = 1.0f;
        return bracket;
      }

      for (int i = 1; i < count; i++)
      {
        const float startTime = timeAt(i - 1);
        const float endTime   = timeAt(i);

        if (time >= startTime && endTime >= time)
        {
          bracket.m_start    = i - 1;
          bracket.m_end      = i;
          bracket.m_duration = endTime - startTime;
          bracket.m_tau      = bracket.m_duration > 0.0f ? (time - startTime) / bracket.m_duration : 0.0f;
          return bracket;
        }
      }

      return bracket;
    }

    /**
     * Samples a position or scale like track at the given time.
     *
     * @param count Number of keys.
     * @param time Sample time in seconds.
     * @param timeAt Callable: key index -> key time in seconds, strictly ascending.
     * @param modeAt Callable: key index -> interpolation mode.
     * @param valueAt Callable: key index -> value.
     * @param out Sampled value. Untouched when the track has no keys.
     * @return False when there is nothing to sample.
     */
    template <typename TimeFn, typename ModeFn, typename ValueFn>
    bool SampleVec3(int count, float time, TimeFn timeAt, ModeFn modeAt, ValueFn valueAt, glm::vec3& out)
    {
      const Bracket bracket = FindBracket(count, time, timeAt);

      if (bracket.m_start < 0)
      {
        return false;
      }

      const glm::vec3 start = valueAt(bracket.m_start);
      const glm::vec3 end   = valueAt(bracket.m_end);

      const KeyInterp startMode = modeAt(bracket.m_start);
      const KeyInterp endMode   = modeAt(bracket.m_end);

      switch (ResolveSegment(startMode, endMode))
      {
      case SegmentKind::Hold:
        // The next key's value shows up exactly on its own frame.
        out = bracket.m_tau >= 1.0f ? end : start;
        return true;

      case SegmentKind::Straight:
        // Mirrors Interpolate() in MathUtil.cpp on purpose: the same operands and the same
        // order keep the result of a Linear clip bit identical to the pre mode engine.
        out = (end - start) * bracket.m_tau + start;
        return true;

      case SegmentKind::Curve:
        break;
      }

      glm::vec3 startTangent(0.0f);
      if (startMode != KeyInterp::Flat)
      {
        if (bracket.m_start == 0)
        {
          // A single neighbour: the segment's own secant, i.e. no implicit ease at the ends.
          startTangent = Secant(start, end, bracket.m_duration);
        }
        else
        {
          const float previousDuration = timeAt(bracket.m_start) - timeAt(bracket.m_start - 1);
          startTangent =
              AutoTangent(valueAt(bracket.m_start - 1), start, end, previousDuration, bracket.m_duration);
        }
      }

      glm::vec3 endTangent(0.0f);
      if (endMode != KeyInterp::Flat)
      {
        if (bracket.m_end == count - 1)
        {
          endTangent = Secant(start, end, bracket.m_duration);
        }
        else
        {
          const float nextDuration = timeAt(bracket.m_end + 1) - timeAt(bracket.m_end);
          endTangent = AutoTangent(start, end, valueAt(bracket.m_end + 1), bracket.m_duration, nextDuration);
        }
      }

      out = HermiteValue(start, end, startTangent, endTangent, bracket.m_duration, bracket.m_tau);
      return true;
    }

    /**
     * Samples a rotation track at the given time.
     *
     * A cubic segment runs the four quaternion components as four curves and normalises the result,
     * the way Blender interpolates a quaternion property. That is not cosmetic: slerp keeps the
     * angular *speed* continuous at a key but not its *direction*, because the segment before a key
     * and the one after it are different geodesics, so the rotation axis jumps where they meet and
     * the rotation whips. Measured on a five key camera fly through that jump was 84 to 120 degrees
     * per key, 100 to 450 rad/s^2 of jerk. Four component curves are C1 across a key, so the whole
     * angular velocity vector stays continuous and the motion reads as an S instead of a hit.
     *
     * Each component takes the same local auto tangent the position and scale channels use
     * (AutoTangent): the average of the two neighbouring secants, limited so a key cannot make its
     * segment overshoot, flattened where the component peaks. Nothing is solved over the track, so a
     * sample costs what it always did, a few dot products and no allocation.
     *
     * @param count Number of keys.
     * @param time Sample time in seconds.
     * @param timeAt Callable: key index -> key time in seconds, strictly ascending.
     * @param modeAt Callable: key index -> interpolation mode.
     * @param valueAt Callable: key index -> value.
     * @param out Sampled value. Untouched when the track has no keys.
     * @return False when there is nothing to sample.
     */
    template <typename TimeFn, typename ModeFn, typename ValueFn>
    bool SampleQuat(int count, float time, TimeFn timeAt, ModeFn modeAt, ValueFn valueAt, glm::quat& out)
    {
      const Bracket bracket = FindBracket(count, time, timeAt);

      if (bracket.m_start < 0)
      {
        return false;
      }

      const glm::quat start = valueAt(bracket.m_start);
      const glm::quat end   = valueAt(bracket.m_end);

      const KeyInterp startMode = modeAt(bracket.m_start);
      const KeyInterp endMode   = modeAt(bracket.m_end);

      switch (ResolveSegment(startMode, endMode))
      {
      case SegmentKind::Hold:
        out = bracket.m_tau >= 1.0f ? end : start;
        return true;

      case SegmentKind::Straight:
        out = glm::slerp(start, end, bracket.m_tau);
        return true;

      case SegmentKind::Curve:
        break;
      }

      // A two key track has no key sitting between two segments, so there is no axis jump to remove
      // and the curve stays the plain slerp it has always played.
      if (count == 2)
      {
        out = glm::slerp(start, end, bracket.m_tau);
        return true;
      }

      // The four component curves run in one frame: an end key in the other hemisphere is negated
      // (the same short way around glm::slerp takes), and the sign it was negated with applies to
      // its tangent too, so the curve and its derivative still describe the same rotation.
      const float endSign = glm::dot(end, start) < 0.0f ? -1.0f : 1.0f;

      const glm::vec4 startComponents(start.x, start.y, start.z, start.w);
      const glm::vec4 endComponents = endSign * glm::vec4(end.x, end.y, end.z, end.w);

      // Local auto tangent of one component at the start key, in the frame of this segment.
      auto startTangent = [&](int axis) -> float
      {
        const float current = startComponents[axis];
        const float next    = endComponents[axis];

        if (bracket.m_start == 0)
        {
          // A single neighbour: the segment's own secant, i.e. no implicit ease at the ends.
          return Secant(current, next, bracket.m_duration);
        }

        const int previousIndex      = bracket.m_start - 1;
        const glm::quat previousKey  = valueAt(previousIndex);
        const glm::quat previous     = glm::dot(previousKey, start) < 0.0f ? -previousKey : previousKey;
        const float previousDuration = timeAt(bracket.m_start) - timeAt(previousIndex);

        return AutoTangent(previous[axis], current, next, previousDuration, bracket.m_duration);
      };

      // Local auto tangent of one component at the end key, in the frame of this segment.
      auto endTangent = [&](int axis) -> float
      {
        const float current  = endComponents[axis];
        const float previous = startComponents[axis];

        if (bracket.m_end == count - 1)
        {
          return Secant(previous, current, bracket.m_duration);
        }

        const int nextIndex      = bracket.m_end + 1;
        const float nextDuration = timeAt(nextIndex) - timeAt(bracket.m_end);

        // The neighbour is aligned to the key it follows, then carried into this segment's frame by
        // the sign the segment's end key was negated with.
        const glm::quat& endKey = valueAt(bracket.m_end);
        const glm::quat nextKey = valueAt(nextIndex);
        const glm::quat next    = endSign * (glm::dot(nextKey, endKey) < 0.0f ? -nextKey : nextKey);

        return AutoTangent(previous, current, next[axis], bracket.m_duration, nextDuration);
      };

      glm::vec4 components;
      for (int axis = 0; axis < 4; axis++)
      {
        const float startSlope = startMode == KeyInterp::Flat ? 0.0f : startTangent(axis);
        const float endSlope   = endMode == KeyInterp::Flat ? 0.0f : endTangent(axis);

        components[axis] = HermiteValue(startComponents[axis],
                                        endComponents[axis],
                                        startSlope,
                                        endSlope,
                                        bracket.m_duration,
                                        bracket.m_tau);
      }

      // glm is built with GLM_FORCE_QUAT_DATA_XYZW, so the four argument constructor takes
      // (x, y, z, w), the order the engine stores and serializes a rotation in.
      const glm::quat sampled(components.x, components.y, components.z, components.w);
      const float length = glm::length(sampled);

      out = length > 0.000001f ? sampled / length : start;
      return true;
    }

  } // namespace Interpolation

} // namespace ToolKit
