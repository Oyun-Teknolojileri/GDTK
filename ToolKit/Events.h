/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Types.h"

namespace ToolKit
{

  /** All possible event action types. */
  enum class EventAction
  {
    Null,
    KeyDown,
    KeyUp,
    LeftClick,
    RightClick,
    MiddleClick,
    Move,
    Scroll,
    GamepadAxis,
    GamepadButtonDown,
    GamepadButtonUp,
    Touch
  };

  /** Base class for all events. */
  class TK_API Event
  {
   public:
    enum class EventType
    {
      Null,
      Mouse,
      Keyboard,
      Gamepad,
      Touch
    };

    EventType m_type     = EventType::Null;
    EventAction m_action = EventAction::Null;
  };

  /** Keyboard input event. */
  class TK_API KeyboardEvent : public Event
  {
   public:
    KeyboardEvent() { m_type = EventType::Keyboard; }

   public:
    /** Key code of the pressed key. */
    int m_keyCode = 0;
    /** Or combination of key modifiers capslock, shift, alt etc... */
    int m_mode    = 0;
  };

  /** Mouse input event. */
  class TK_API MouseEvent : public Event
  {
   public:
    MouseEvent() { m_type = EventType::Mouse; }

   public:
    /** States if the mouse button is released. True means button is up. */
    bool m_release  = false;

    /** Mouse position in application window coordinates */
    int absolute[2] = {0, 0};
    /** Mouse delta move. */
    int relative[2] = {0, 0};
    /** Mouse scroll delta. */
    int scroll[2]   = {0, 0};
  };

  class TK_API TouchEvent : public Event
  {
   public:
    TouchEvent() { m_type = EventType::Touch; }

   public:
    /** States whether touch down or up. True means touch released. */
    bool m_release    = false;

    /** Normalized x,y coordinates. Multiply by screen resolution to get pixel position. */
    float absolute[2] = {0.0f, 0.0f};

    /** Normalized delta x,y coordinates. Multiply by screen resolution to get pixel position. */
    float relative[2] = {0.0f, 0.0f};

    /** Angle between fingers in radiance. Valid when finger count > 1. */
    float theta       = 0.0f;

    /**
     * Normalized delta distance between fingers.
     * Positive values means fingers get away from each other. Vise versa for negative values.
     * Valid when finger count > 1.
     */
    float distance    = 0.0f;

    /** Normalized center point of the touch event. Multiply by screen resolution to get pixel position. */
    float center[2]   = {0.0f, 0.0f};

    /** Number of fingers on the screen. */
    int fingerCount   = 1;
  };

  enum class GamepadButton : uint
  {
    None          = 0,
    A             = 1 << 0,
    Cross         = 1 << 0, //!< (PS) Cross    X  =  (Xbox) A
    B             = 1 << 1,
    Circle        = 1 << 1, //!< (PS) Circle   () =  (Xbox) B
    Y             = 1 << 2,
    Square        = 1 << 2, //!< (PS) Square   [] =  (Xbox) Y
    X             = 1 << 3,
    Triangle      = 1 << 3, //!< (PS) Triangle /\ =  (Xbox) X
    Back          = 1 << 4, //!< Select
    Guide         = 1 << 5, //!< Mode
    Start         = 1 << 6,
    LeftStick     = 1 << 7,
    RightStick    = 1 << 8,
    LeftShoulder  = 1 << 9,  //!< L1
    RightShoulder = 1 << 10, //!< R1
    DpadUp        = 1 << 11,
    DpadDown      = 1 << 12,
    DpadLeft      = 1 << 13,
    DpadRight     = 1 << 14,
    Misc1 = 1 << 15, //!< Xbox Series X share button, PS5 microphone button, Nintendo Switch Pro capture button, Amazon
                     //!< Luna microphone button
    Paddle1  = 1 << 16, //!< Xbox Elite paddle P1
    Paddle2  = 1 << 17, //!< Xbox Elite paddle P3
    Paddle3  = 1 << 18, //!< Xbox Elite paddle P2
    Paddle4  = 1 << 19, //!< Xbox Elite paddle P4
    Touchpad = 1 << 20, //!< PS4/PS5 touchpad button
    MaxBit   = 1 << 21, //!< You can use when you iterate through bits
    Count    = 21
  };

  class TK_API GamepadEvent : public Event
  {
   public:
    GamepadEvent() { m_type = EventType::Gamepad; }

    enum class StickAxis
    {
      LeftX,
      LeftY,
      RightX,
      RightY,
      TriggerLeft,
      TriggerRight
    };

    float m_angle;
    StickAxis m_axis;
    GamepadButton m_button;
  };

} // namespace ToolKit
