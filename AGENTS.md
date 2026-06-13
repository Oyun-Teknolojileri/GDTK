# GDTK Coding Standards

This file defines the coding standards for the GDTK / ToolKit project. All code, comments, and documentation must follow these rules.

---

## Language & Character Set

- **English only** - all code, comments, variable names, documentation, and strings must be in English.
- **ASCII 128 only** - files must contain only ASCII characters (0-127). No Unicode, no non-English characters, no special symbols outside ASCII range.

---

## Code Style

### Formatting (.clang-format rules)

- Style: Microsoft-based
- Brace style: Allman (braces on their own line)
- Indentation: 2 spaces (tab width = 1)
- Column limit: 120 characters
- Pointer alignment: Left (`T* var`, not `T *var`)
- Namespace indentation: All (fully indented nested namespaces)
- C++ standard: C++17
- Bin pack arguments/parameters: false (one parameter per line)

### Naming Conventions

| Element              | Convention               | Example                     |
|----------------------|--------------------------|-----------------------------|
| Member variables     | `m_memberName`           | `m_node`, `m_components`     |
| Private/internal     | `_underscorePrefix`      | `_parentId`, `_prefabRoot`  |
| Class macros         | `TKDeclareClass`, `TKDeclareParam` | `TKDeclareClass(Entity, Object)` |
| API exports          | `TK_API`                 | `class TK_API Entity`       |
| Smart pointers       | `MakeNewPtr<T>()`        | `MakeNewPtr<Mesh>()`         |
| Base class calls     | `Super::Method()`        | `Super::NativeConstruct()`  |

### Class Structure

```cpp
class TK_API ClassName : public BaseClass
{
 public:
   ClassName();
   virtual ~ClassName();

   // Public methods
   void PublicMethod();

 protected:
   virtual void ProtectedMethod();
   virtual void AnotherProtected();

 public:
   // Public members
   int m_publicMember = 0;

 protected:
   // Protected members
   int m_protectedMember = 0;

 private:
   // Private members
   int m_privateMember = 0;
};
```

---

## Header Conventions

1. License header (copyright notice)
2. `#pragma once`
3. Docstring: `/** @file ClassName.h ... */`
4. System includes (alphabetically grouped)
5. Local includes
6. `namespace ToolKit { ... }`
7. Code

**Example header structure:**

```cpp
/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, please visit [otyazilim.com].
 */

#pragma once

/**
 * @file Entity.h Header for Entity
 */

#include "ToolKit.h"
#include "Types.h"

namespace ToolKit
{

  class TK_API Entity : public Object
  {
   public:
     TKDeclareClass(Entity, Object);

     Entity();
     virtual ~Entity();

     void NativeConstruct() override;

     /** Returns the parent entity if any exist. */
     EntityPtr Parent() const;

    public:
     TKDeclareParam(String, Name);
     Node* m_node;

   private:
     ComponentPtrArray m_components;
  };

} // namespace ToolKit
```

---

## Comments

- All comments must be in **English only**.
- Use Javadoc style for function documentation:
  ```cpp
  /** Returns the bounding box of the entity. */
  const BoundingBox& GetBoundingBox(bool inWorld = false);

  /**
   * Remove the given component from the components of the Entity.
   * @param componentId Id of the component to be removed.
   * @return Removed ComponentPtr. If nothing gets removed, returns nullptr.
   */
  ComponentPtr RemoveComponent(ClassMeta* Class);
  ```
- Section separators: `////////////////////////////////////////`

---

## File Organization

### Include Order

```cpp
// 1. Related header (implementation file only)
#include "Entity.h"

// 2. Project headers
#include "AABBTree.h"
#include "Animation.h"

// 3. System headers
#include <vector>
#include <string>

// 4. Third-party headers (with priority grouping via .clang-format)
#include "glad/gl.h"
```

### File Naming

- Header files: `ClassName.h`
- Implementation: `ClassName.cpp`
- Match class name to file name exactly.

---

## General Rules

1. **No magic numbers** - use named constants.
2. **No `using namespace` in headers** - always fully qualify names.
3. **Const correctness** - mark parameters and methods `const` where applicable.
4. **Override specifier** - always use `override` for overridden virtual methods.
5. **RAII** - use smart pointers (`std::shared_ptr`, `MakeNewPtr<T>()`) for dynamic allocation.
6. **No raw `new`/`delete`** - use `MakeNewPtr<T>()` and `SafeDel(obj)`.
7. **Header-only when possible** - simple template classes can be header-only.
8. **Serialization** - implement `SerializeImp` and `DeSerializeImp` for serializable classes.
9. **Thread safety** - mark thread-unsafe members and document mutex usage.

---

## Enforcement

- All rules in this file are enforced via `.clang-format` (formatting) and code review.
- Non-ASCII characters found in files must be reported and fixed.
- Non-English comments must be reported and fixed.
- Run `clang-format -i file.cpp` before committing.

---

## Documentation Maintenance (`gdtk-overview.md`)

`gdtk-overview.md` is the project's architectural / context file. It is the first
thing read at the start of every working session, so it MUST stay in sync with the
codebase. The agent maintaining the repo (human or AI) is responsible for updating
it after every meaningful change.

### When to update

Update `gdtk-overview.md` after a change that affects any of:

- **Project layout / solution structure** — new folder, new vcxproj, file moved
  out of its old module, project renamed, new template added.
- **Build / dependency pipeline** — new vendored dep, dep swapped, build script
  behavior change (e.g. `BuildDependencies.bat` flow, output naming, generator
  flags), toolchain version bump, new compile define that changes the public
  surface (`-DTK_GL_ES_3_0` etc.).
- **Core engine architecture** — new manager on the `Main` singleton, new
  subsystem, new render path, new pass, new UBO slot, new RHI backend, new
  resource type, new scene/ECS concept, new threading primitive.
- **Editor / tool surface** — new editor window, new command, new plugin type,
  new project template, new import format, new packer mode.
- **Public API breakage or rename** — class/function/file renamed or removed,
  serialization format version bump, behavior contract change for an existing
  API.
- **Section 14 (Quick File Lookup) drift** — a header listed there moved or no
  longer exists, or a new key header is missing from the table.
- **Section 13 / Section 1 paths or facts** — repo path, solution path, license,
  supported platforms / publish targets, primary build environment.

### When NOT to update

Skip the update for purely local changes that don't affect any of the above:
typo fixes, internal refactors with no API change, bug fixes that don't touch
the public surface, formatting / `.clang-format` tweaks, comments-only edits,
single-file optimizations.

### How to update

1. Read the current `gdtk-overview.md` and locate the section(s) affected
   (Section 2 layout, Section 3 engine core, Section 4 rendering, Section 14
   file lookup, etc.).
2. Apply the minimal edit — adjust the existing prose or table row, do not
   duplicate content into a new section if it belongs in an existing one.
3. If a new area is introduced that no current section covers, add a new
   subsection in the right place and link it from Section 14 if a key file
   is involved.
4. Keep the tone and structure consistent with the rest of the file (no
   marketing language, no speculation, no "TODO" placeholders that aren't
   actually TODO'd in the code).
5. If the change also touches a section in `AGENTS.md` (this file), update
   both in the same commit.
6. Mention the overview update in the commit message, e.g.
   `docs(overview): ...` or `chore(overview): sync after <change>`.

### Examples of "meaningful change" vs "not"

- Meaningful: assimp output name flattened from `assimp-vc143-mt.lib` to
  `assimp.lib` (build pipeline change, downstream link line affected).
  -> Update Section 12 (Build & Run) and Section 14 if a new wrapper file
  becomes notable.
- Not meaningful: a one-line typo fix in a comment, a 5% faster inner loop
  in a pass, fixing a use-after-free in `Mesh::Init`.

### Staleness check

At the start of a session, if the agent spots that `gdtk-overview.md` no
longer matches the codebase (e.g. a listed file no longer exists, a manager
on `Main` is missing, a referenced vcxproj is gone), it MUST fix the overview
in the same pass instead of silently working around the drift.
