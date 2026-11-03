/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file EditorApi.h The surface the editor exports to its modules.
 *
 * A module links against the editor and calls these functions to plug itself
 * into the running editor: register console commands, write into the console.
 * Keeping the surface this small is the point - a module that needs more than a
 * command table and a log line is usually a module that should have been editor
 * code.
 *
 * The editor side of the contract is ConsoleWindow: it drains the same registry
 * for its command list, its auto complete and its command execution, so a
 * command a module registers behaves exactly like a built in one.
 *
 * Rules for the executors:
 *   - they are called on the main thread, from the console or the frame loop;
 *   - they must not capture engine objects. The registry outlives the windows
 *     and the scene, so a captured Entity/Mesh/Viewport would keep it alive past
 *     the engine teardown (see "Object Lifetime & Shutdown Order" in AGENTS.md).
 *     A plain function or a lambda over numbers and strings is what belongs
 *     here.
 */

#include "EditorTypes.h"

#include <Logger.h>

namespace ToolKit
{
  namespace Editor
  {

    // Command line word processing. "<Command> arg --tag value" is parsed into
    // a named command and a list of tag/value pairs; a single argument without
    // a tag lands under an empty tag name.
    typedef std::pair<String, StringArray> TagArg;
    typedef std::vector<TagArg> TagArgArray;
    typedef std::function<void(TagArgArray)> ConsoleCommandFn;

    /**
     * Adds a command to the console, the command palette and the auto complete.
     * @param name Command as the user types it, case sensitive.
     * @param fn The executor. See the rules above for what it may capture.
     * @return False when the name is already taken; the first registration wins,
     * so a module cannot shadow a built in command.
     */
    TK_EDITOR_API bool RegisterConsoleCommand(const String& name, const ConsoleCommandFn& fn);

    /**
     * Removes a command. Modules call this from their shutdown so the console
     * never holds a pointer into code that is going away.
     * @return False when no command with that name is registered.
     */
    TK_EDITOR_API bool UnregisterConsoleCommand(const String& name);

    /** Returns the executor for a command, or an empty function when there is none. */
    TK_EDITOR_API ConsoleCommandFn FindConsoleCommand(const String& name);

    /** Names of every registered command, sorted. */
    TK_EDITOR_API StringArray GetConsoleCommandNames();

    /**
     * Writes a line into the editor console. Works before the console window
     * exists and after it is gone - the line is dropped in the second case - so
     * a module can report its own startup.
     * @param message The text to show.
     * @param type Severity, which picks the colour and the filter bucket.
     */
    TK_EDITOR_API void WriteConsoleLine(const String& message, LogType type = LogType::Memo);

  } // namespace Editor
} // namespace ToolKit
