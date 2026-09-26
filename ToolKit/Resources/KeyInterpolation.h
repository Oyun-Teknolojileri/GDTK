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
     * Angle between two quaternions in radians.
     *
     * Computed as 2 * atan2(|v|, w) of the relative rotation rather than 2 * acos(dot): a dot
     * product near 1 keeps only a fraction of its precision, and the neighbouring keys of a track
     * are usually close, so acos would report a sizeable speed for a segment that barely rotates.
     * The relative rotation is hemisphere aligned first (the shorter arc), the convention
     * glm::slerp applies internally.
     */
    inline float QuatAngle(const glm::quat& start, const glm::quat& end)
    {
      glm::quat relative = start * glm::inverse(end);

      if (relative.w < 0.0f)
      {
        relative = -relative;
      }

      return 2.0f * std::atan2(glm::length(glm::vec3(relative.x, relative.y, relative.z)), relative.w);
    }

    /** Angular speed of a segment if it runs straight: radians per second. */
    inline float AngularSpeed(const glm::quat& start, const glm::quat& end, float duration)
    {
      return duration > 0.0f ? QuatAngle(start, end) / duration : 0.0f;
    }

    /**
     * Slope of the eased slerp parameter at a key: the key's angular speed expressed in
     * parameter units, where 1.0 means "this segment runs at its own average speed".
     */
    inline float EaseSlope(float speed, float segmentSpeed) { return segmentSpeed > 0.0f ? speed / segmentSpeed : 1.0f; }

    /**
     * Angular speed a key may carry, limited the same way the position tangents are (LimitSlope):
     * the cap comes from the slower of the two segments, so the speed the key leaves with is the
     * speed it arrived with.
     */
    inline float LimitAngularSpeed(float speed, float previousSegmentSpeed, float nextSegmentSpeed)
    {
      return std::min(speed, MaxCubicSlope * std::min(previousSegmentSpeed, nextSegmentSpeed));
    }

    /** Keeps the parameter monotone, so the eased slerp never overshoots its target key. */
    inline float ClampEaseSlope(float slope) { return glm::clamp(slope, 0.0f, MaxCubicSlope); }

    /**
     * Eased slerp parameter of a segment whose end slopes are given in parameter units.
     * Slope 0 means "leaves from rest" (Flat), slope 1 is the segment's own average speed.
     */
    inline float EaseParameter(float startSlope, float endSlope, float tau)
    {
      // Clamped here as well, so no caller can hand in a parameter curve that overshoots the key
      // it heads for or backtracks inside its segment.
      startSlope = ClampEaseSlope(startSlope);
      endSlope   = ClampEaseSlope(endSlope);

      // Both ends at the segment's own speed is the linear parameter. Returned as is, so a track of
      // evenly spaced rotations matches plain slerp to the last bit.
      if (startSlope == 1.0f && endSlope == 1.0f)
      {
        return tau;
      }

      return (Hermite10(tau) * startSlope) + Hermite01(tau) + (Hermite11(tau) * endSlope);
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
     * Samples a rotation track at the given time. The cubic modes use slerp with an eased
     * parameter whose end slopes are matched to the angular speed of the neighbouring segments,
     * so the angular velocity is continuous at a key without needing quaternion tangents.
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

      const float segmentSpeed = AngularSpeed(start, end, bracket.m_duration);

      // The key's angular speed, averaged over the segment it arrives on and the one it leaves on,
      // then limited so both sides stay monotone. Both sides read the same value, so the angular
      // velocity is continuous at the key.
      float startSpeed = segmentSpeed;
      if (bracket.m_start > 0)
      {
        const float previousDuration = timeAt(bracket.m_start) - timeAt(bracket.m_start - 1);
        const float previousSpeed    = AngularSpeed(valueAt(bracket.m_start - 1), start, previousDuration);
        startSpeed = LimitAngularSpeed((previousSpeed + segmentSpeed) * 0.5f, previousSpeed, segmentSpeed);
      }

      float endSpeed = segmentSpeed;
      if (bracket.m_end < count - 1)
      {
        const float nextDuration = timeAt(bracket.m_end + 1) - timeAt(bracket.m_end);
        const float nextSpeed    = AngularSpeed(end, valueAt(bracket.m_end + 1), nextDuration);
        endSpeed = LimitAngularSpeed((segmentSpeed + nextSpeed) * 0.5f, segmentSpeed, nextSpeed);
      }

      const float startSlope = startMode == KeyInterp::Flat ? 0.0f : EaseSlope(startSpeed, segmentSpeed);
      const float endSlope   = endMode == KeyInterp::Flat ? 0.0f : EaseSlope(endSpeed, segmentSpeed);

      out = glm::slerp(start, end, EaseParameter(startSlope, endSlope, bracket.m_tau));
      return true;
    }

  } // namespace Interpolation

} // namespace ToolKit
