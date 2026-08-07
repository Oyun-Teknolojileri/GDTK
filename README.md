# ToolKit

<img src="Resources/Engine/Textures/Icons/app.png" width="48" align="left">ToolKit is a 3d editor & interactive application development platform. It allows users to create 3d scenes and bring in the interactivity via C++ plug-ins.

## Motivation

### Simplicity

Many games and interactive projects don't require the vast toolsets offered by engines like Unity, Unreal, or even Godot. For developers seeking a clearer understanding and greater control over their work, a simpler engine often leads to better results. ToolKit embraces this philosophy by offering a minimal yet powerful alternative that emphasizes clarity, performance, and modern architecture.

ToolKit is not designed to compete with enterprise-level engines. Instead, it is tailored for indie developers, researchers, and creative technologists who value understanding the entire stack and want to build things their way.

ToolKit is built with a strong emphasis on modularity. Instead of bundling everything into the core, we develop all non-essential features such as editors, scripting, physics, and more as separate modules. This approach keeps the engine lightweight, bloat-free, and highly customizable, empowering developers to use only what they need while keeping full control over their stack. Our goal is to maintain a modern, minimal core that can be extended cleanly through well-defined modules.

### Community

We believe that great tools are shaped by communities with shared values. Like Blender and Godot, ToolKit aims to foster a culture of openness, simplicity, and collaborative innovation. Whether you're here to learn, contribute, or build something entirely new, ToolKit is a platform for developers who seek to create unique and modern experiences without the bloat.

If you value simplicity over complexity, learning over abstraction, and collaboration over competition, we invite you to join us.

## Platforms

ToolKit currently builds and runs on **Windows** and **Linux**. The primary development OS for the Editor is Windows.

ToolKit can publish for:

- Windows exe
- Web html + .wasm or .js
- Android apk

All the publishing can be achieved from within the editor via click of a button. However for publishing to given platforms there are required configuration steps and installations such as emscripten and the Android SDK.

![ToolKit editor](Images/tk_ed_21.gif)

## Projects Using ToolKit

![Multiverse Go game made with ToolKit](Images/tk_ed_22.gif)

[**Multiverse Go**](https://store.steampowered.com/app/2346880/Multiverse_GO/)

Multiverse GO is a turn-based puzzle adventure set in a group of multiple universes. You can explore the multiverse and face challenges. The game focuses on forward thinking to progress through levels. Check out the link for more visuals from the project.

**Don't Forget to Wishlist the Game in Steam !**

## Building

ToolKit uses CMake for everything. There are two separate CMake builds: the vendored dependencies under `Dependency/` and the engine + tools (Editor, Launcher, Import, Packer) at the repository root. The recommended way to drive both is the Python build scripts under `BuildScripts/`, which behave the same on Windows and Linux. Raw CMake commands are also given below for advanced usage.

### Requirements

| Tool | Notes |
|------|-------|
| Git | To clone the repo and pull the dependency submodules. |
| CMake | 3.16 or newer. |
| Python | 3.6 or newer (standard library only). Drives the build scripts and the optional GUI. |
| C/C++ compiler | Windows: MSVC via Visual Studio 2022 or newer (Desktop development with C++ workload). Linux: GCC or Clang. |
| Ninja | Optional but strongly recommended - much faster than the default generators. |
| VSCode + C/C++ Extension Pack | Only needed for game / plug-in development. VSCode is the default code editor ToolKit integrates with. |

Make sure `git` and `cmake` (and `ninja` if installed) are recognized from the console, as in the screenshot below.

![Console check](Images/tk_cmd.png)

#### Linux only: X11 development packages

SDL2's Linux video backend is X11, so the matching development headers must be installed **before** the dependency build:

- Fedora / RHEL: `sudo dnf install libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXinerama-devel libxkbcommon-devel`
- Debian / Ubuntu: `sudo apt install libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxkbcommon-dev`
- Arch: `sudo pacman -S libx11 libxext libxrandr libxcursor libxi libxinerama libxkbcommon`

The build script checks these automatically and aborts with the exact install command if anything is missing. Pass `--install-deps` to have it install the packages for you.

### Quick start (recommended)

```bash
git clone https://github.com/Oyun-Teknolojileri/GDTK.git
cd GDTK

# One command: pulls submodules, builds the dependencies, builds the
# engine and tools, then installs everything into Bin<Config>/.
# On Windows use "python" instead of "python3".
python3 BuildScripts/build_gdtk.py --configs Debug
```

The script auto-invokes the dependency build when the dependency tree is missing, so a cold clone only needs this one command. On Windows it picks the Visual Studio generator automatically when run from a developer prompt (or Ninja when available), so no generator setup is needed. Useful variations:

```bash
python3 BuildScripts/build_gdtk.py --configs Debug Release  # both configs (the default)
python3 BuildScripts/build_gdtk.py --target Editor          # rebuild only one target
python3 BuildScripts/build_gdtk.py --clean                  # wipe Intermediate/ first
```

**Prefer a GUI?** `BuildScripts/build_ui.py` is a small Tkinter front end (standard library only, nothing to install) that exposes every flag of the build scripts, shows a live command preview and streams the build log:

```bash
python3 BuildScripts/build_ui.py
```

See `BuildScripts/README.md` for the full flag reference.

### Manual CMake (advanced)

The two builds are independent: dependencies first, then the engine. Initialize the submodules before the dependency build:

```bash
git submodule update --init --recursive
```

**1. Dependencies**

Linux:

```bash
cmake -S Dependency -B Dependency/Intermediate/Linux/Debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DTK_PLATFORM=Linux -DTOOLKIT_DIR=$PWD
cmake --build Dependency/Intermediate/Linux/Debug --parallel
```

Windows (Ninja, from an x64 Native Tools prompt or any shell with `cl` on PATH):

```bat
cmake -S Dependency -B Dependency\Intermediate\Windows\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTK_PLATFORM=Windows -DTOOLKIT_DIR=%CD%
cmake --build Dependency\Intermediate\Windows\Debug --parallel
```

The Python script additionally stages small `find_package` wrapper configs (`Dependency/Config/*-config.cmake`) next to the built artifacts - the engine configure step needs them. If you drive CMake by hand, copy them yourself (Linux example):

```bash
for f in Dependency/Config/*-config.cmake; do
  dep=$(basename "$f" -config.cmake)
  mkdir -p "Dependency/Intermediate/Linux/Debug/$dep"
  cp "$f" "Dependency/Intermediate/Linux/Debug/$dep/${dep}Config.cmake"
done
```

**2. Engine + tools**

Linux:

```bash
cmake -S . -B Intermediate/Linux/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build Intermediate/Linux/Debug --parallel $(nproc)
cmake --install Intermediate/Linux/Debug --prefix .
```

Windows:

```bat
cmake -S . -B Intermediate\Windows\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build Intermediate\Windows\Debug --parallel
cmake --install Intermediate\Windows\Debug --prefix .
```

### Output

All binaries and shared dependencies land in `Bin<Config>/` (e.g. `BinDebug/`):

- `Editor`, `Launcher`, `Import`, `Packer` - executables
- `ToolKitd.dll` / `libToolKitd.so` - engine library (`d` postfix in Debug)
- `Workspace` - static helper library
- SDL2, imgui, assimp shared libraries

After a successful build + install, `Bin<Config>/` is self-contained: on Linux every binary finds its dependencies via `$ORIGIN` RPATH (no `LD_LIBRARY_PATH` needed) and on Windows every required DLL sits next to the executables.

### Optional: Vulkan backend

The default render backend is OpenGL. An opt-in Vulkan backend can be enabled with `python3 BuildScripts/build_gdtk.py --vulkan`, which requires the platform's Vulkan SDK / dev packages. See `BuildScripts/README.md` for details. Note that the Launcher currently only works with the OpenGL backend.

## Setup & First Run

The easiest way to get going is the **Launcher** - a project browser that manages the workspace and your projects, so there is nothing to configure by hand. Run it from the output folder:

```bash
# Windows
BinDebug\Launcher.exe

# Linux
./BinDebug/Launcher
```

On first run the Launcher asks for a workspace directory. Pick a folder where you have full read & write access - all projects and their files get stored under it. A sample path:

> "C:/Users/**YourUserName**/Documents/TK-Workspace"

![ToolKit setting a workspace](Images/tk_workspace.png)

From the Launcher you can:

- Create a new project from the built-in template, or clone one from a git URL.
- Open a project - this starts the Editor with the project loaded.
- Delete projects, search / filter the project list and create desktop shortcuts for quick access.

Editor settings and UI state are stored per user under `%APPDATA%/ToolKit` on Windows and `~/.config/ToolKit` (or `$XDG_CONFIG_HOME/ToolKit`) on Linux. When you need to reset the settings for troubleshooting purposes, feel free to remove that directory - it will be recreated with default settings.

You can also run the Editor directly (`BinDebug/Editor(.exe)`). It asks for the workspace directory the same way, and projects can also be created from its main menu bar.

## Creating A New Project

The recommended path is the Launcher's new project dialog, which sets everything up in the workspace automatically. Alternatively, use the main menu bar in the editor. Either way, the project name must consist of ASCII alphanumeric characters without any whitespace. Publish name can be anything but the project name has this requirement.

![ToolKit new project menu](Images/tk_newproject.png)

Visual Studio Code (VSCode) is the default code editor used by the ToolKit. You'll need to install it to write game / application codes. Also VSCode C/C++ Extension Pack must be installed.

In order to see the project's code folder and start development, you can press the VSCode icon in the Simulation window. It will open the "Workspace/Project/Codes" folder in VSCode with boiler plate codes to let you start development immediately. You can compile the project from the editor by pressing the Build button in the Simulation window ( next to VSCode icon ) or just compile it in the VSCode itself. Then you can press the play button to run the simulation in the editor. In VSCode you can compile the project with F7 also you can attach the VSCode to the ToolKit Editor to debug your codes when you are running the simulation from the editor.

![ToolKit simulation window](Images/tk_simuation.png)

## Dependencies
- stb_image - MIT 
- SDL 2.0 - Zlib
- rapidxml - MIT
- MiniAudio - MIT
- glm - MIT
- glad - MIT
- Dear imgui - MIT
- Assimp - BSD
- minizip-ng - Zlib

## License

 Source code is dual-licensed. It is available under the terms of the GNU Lesser General Public License v3.0 (LGPL-3.0) for open-source use. Additionally, we offer a proprietary license with more permissive terms suitable for commercial applications.
 For information on using the open-source LGPL v3 license, please refer to the accompanying LICENSE file. If you require a more flexible commercial license for proprietary projects or custom development, please contact us for personalized licensing terms and conditions. 

 [OtSoftware](https://www.otyazilim.com)
 
## Final Words

Project is in active development. Feel free to play around with it and get in touch with [us](https://www.otyazilim.com)

Enjoy!
