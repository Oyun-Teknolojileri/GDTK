/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "EditorApi.h"

#include "App.h"
#include "ConsoleWindow.h"

#include <map>
#include <mutex>

namespace ToolKit
{
  namespace Editor
  {

    namespace
    {

      /**
       * The command table lives here rather than inside ConsoleWindow because a
       * module comes up before any window exists and has no way to reach one.
       * The console reads this table for its command list, its auto complete and
       * its execution, so both registration paths end up in one place.
       *
       * Process lifetime state, but it holds no engine object: the entries are
       * `std::function`s over numbers and strings (see EditorApi.h), and the
       * editor's own executors are plain function pointers.
       */
      std::mutex& CommandMutex()
      {
        static std::mutex mutex;
        return mutex;
      }

      std::map<String, ConsoleCommandFn>& CommandTable()
      {
        static std::map<String, ConsoleCommandFn> commands;
        return commands;
      }

      /**
       * The Modules menu is drawn from this table by UI::ShowMenuModules. It is
       * a flat list in registration order rather than a tree: grouping by module
       * happens at draw time, so a module can register from wherever it comes up
       * without the editor having to know the module set in advance.
       *
       * Same lifetime rule as the command table: the entries are `std::function`s
       * over numbers and strings, never engine objects.
       */
      std::mutex& ModuleMenuMutex()
      {
        static std::mutex mutex;
        return mutex;
      }

      ModuleMenuItemArray& ModuleMenuTable()
      {
        static ModuleMenuItemArray items;
        return items;
      }

    } // namespace

    bool RegisterConsoleCommand(const String& name, const ConsoleCommandFn& fn)
    {
      if (name.empty() || fn == nullptr)
      {
        return false;
      }

      std::lock_guard<std::mutex> lock(CommandMutex());

      if (CommandTable().find(name) != CommandTable().end())
      {
        TK_WRN("Console command '%s' is already registered.", name.c_str());
        return false;
      }

      CommandTable()[name] = fn;
      return true;
    }

    bool UnregisterConsoleCommand(const String& name)
    {
      std::lock_guard<std::mutex> lock(CommandMutex());
      return CommandTable().erase(name) > 0;
    }

    ConsoleCommandFn FindConsoleCommand(const String& name)
    {
      std::lock_guard<std::mutex> lock(CommandMutex());

      const auto command = CommandTable().find(name);
      if (command == CommandTable().end())
      {
        return nullptr;
      }

      return command->second;
    }

    StringArray GetConsoleCommandNames()
    {
      std::lock_guard<std::mutex> lock(CommandMutex());

      StringArray names;
      names.reserve(CommandTable().size());
      for (const auto& command : CommandTable())
      {
        names.push_back(command.first);
      }

      return names;
    }

    void WriteConsoleLine(const String& message, LogType type)
    {
      // Called before the editor has an App (module startup) and after it is
      // gone (shutdown); the line is simply dropped in both cases.
      Editor::App* app = GetApp();
      if (app == nullptr)
      {
        return;
      }

      if (ConsoleWindowPtr console = app->GetConsole())
      {
        console->AddLog(message, type);
      }
    }

    bool RegisterModuleMenuItem(const String& moduleName,
                                const String& label,
                                const MenuActionFn& action,
                                const MenuEnabledFn& enabled,
                                const String& shortcut)
    {
      if (moduleName.empty() || label.empty() || action == nullptr)
      {
        return false;
      }

      std::lock_guard<std::mutex> lock(ModuleMenuMutex());

      for (const ModuleMenuItem& item : ModuleMenuTable())
      {
        if (item.m_module == moduleName && item.m_label == label)
        {
          TK_WRN("Module '%s' already registered the menu entry '%s'.", moduleName.c_str(), label.c_str());
          return false;
        }
      }

      ModuleMenuItem item;
      item.m_module   = moduleName;
      item.m_label    = label;
      item.m_action   = action;
      item.m_enabled  = enabled;
      item.m_shortcut = shortcut;

      ModuleMenuTable().push_back(item);
      return true;
    }

    void UnregisterModuleMenu(const String& moduleName)
    {
      std::lock_guard<std::mutex> lock(ModuleMenuMutex());

      ModuleMenuItemArray& items = ModuleMenuTable();
      for (ModuleMenuItemArray::iterator it = items.begin(); it != items.end();)
      {
        if (it->m_module == moduleName)
        {
          it = items.erase(it);
        }
        else
        {
          ++it;
        }
      }
    }

    ModuleMenuItemArray GetModuleMenuItems()
    {
      std::lock_guard<std::mutex> lock(ModuleMenuMutex());
      return ModuleMenuTable();
    }

  } // namespace Editor
} // namespace ToolKit
