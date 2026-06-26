/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Platform.h"

// STL
#include <assert.h>

#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// GLM
#ifndef TK_GLM
  #define TK_GLM
  #define GLM_FORCE_QUAT_DATA_XYZW
  #define GLM_FORCE_CTOR_INIT
  #define GLM_ENABLE_EXPERIMENTAL
  #define GLM_FORCE_ALIGNED_GENTYPES
  #define GLM_FORCE_INTRINSICS
#endif

#ifdef TK_VULKAN
  #define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/scalar_relational.hpp>

// RapidXml
#include <RapidXml/rapidxml_ext.h>
#include <RapidXml/rapidxml_utils.hpp>

// ToolKit
#include "Entity.h"
#include "Events.h"
#include "Logger.h"
#include "Pass.h"
#include "Serialize.h"
#include "Threads.h"

#ifdef TK_EDITOR
  // Editor-only forward declarations. App.h and EditorRenderer.h
  // pull in TK_EDITOR_API (from EditorTypes.h) and the editor-side
  // ComponentPtr definitions, both of which are required by every
  // Editor translation unit. Adding them here lets the editor use
  // the same PCH as the engine without keeping a separate header.
  #include "EditorTypes.h"
  #include "App.h"
  #include "EditorRenderer.h"
#endif
