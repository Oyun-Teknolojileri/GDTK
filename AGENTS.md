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

- All rules in this file are enforced via Visual Studio's built-in formatter
  (driven by `.clang-format` at the repo root) and code review.
- Non-ASCII characters found in files must be reported and fixed.
- Non-English comments must be reported and fixed.
- **Formatting**: Visual Studio picks up the project's `.clang-format`
  automatically. After editing any C/C++ file the agent must trigger
  "Format Document" (Ctrl+K, Ctrl+D) on the touched file(s) -- VS's
  built-in clang-format is the formatter, no extra tooling required.
  Apply per-file, not per-folder, to keep diffs tight.
  - Edit > Advanced > "Format Document" applies the project rules to the
    active file. Configure "Format on Save" under
    Tools > Options > Text Editor > C/C++ > Formatting if desired.

---

## Pass / PassRequirements Conventions

A pass is a single step in the render pipeline (bloom downsample, gamma tonemap,
forward opaque draw, etc.). Passes declare **what they need** via `PassRequirements`
and `Renderer::ApplyRequirements` does the actual binding in a deterministic order.
This replaces the older ad-hoc pattern of calling `renderer->SetTexture` /
`SetFramebuffer` / `BindProgram` from inside the pass.

### Rule of thumb

- **Declare, don't mutate.** A pass populates `m_requirements` in `PreRender()` (or in
  a callback like `onPreRender`) and then calls `ApplyRequirements(GetRenderer())`
  once. `Render()` itself becomes a pure draw call.
- **No manual `BindUniformBuffer` for slot 7 unless the pass does not use
  `ApplyRequirements`.** The descriptor-set binding for shared slots (slot 7 is the
  pass-specific UBO slot shared by Bloom, DoF, SSAO, gamma, outline, grid, gradient
  sky) is managed by `ApplyRequirements` reading `m_requirements.customUbos`.
- **Two full-quad passes per phase** when a pass uses two fragment shaders that
  must stay attached to their programs (e.g. Bloom's `m_downPass` + `m_upPass`).
  Sharing one quad across both phases let downsample state leak into the upsample draw.

### `PassRequirements` fields

| Field             | When to set                                                    |
|-------------------|----------------------------------------------------------------|
| `fragmentShader`  | Always (must be non-null before ApplyRequirements)             |
| `vertexShader`    | Always (must be non-null before ApplyRequirements)             |
| `program`         | Only if you already have a built `GpuProgramPtr` to reuse       |
| `frameBuffer`     | Where the pass writes color/depth                              |
| `clearBits`       | Color/depth/stencil mask -- defaults to `None`                  |
| `textures`        | Slot-indexed sampler bindings (`std::unordered_map<int, TexturePtr>`) |
| `semanticTextures`| Name-indexed sampler bindings (`std::unordered_map<String, TexturePtr>`). Resolved to slot AFTER program is bound -- safer than `textures` for unknown binding order. |
| `customUbos`      | Slot-indexed pass-specific UBO bindings (`std::unordered_map<int, UniformBuffer*>`) |
| `defines`         | `std::unordered_map<String, String>` of `#define`s applied to fragment shader before program build |
| `passState`       | Passive pipeline state (`RenderState` -- depth, blend override, stencil, scissor) |
| `scissorEnabled` / `scissor` | Per-pass scissor rectangle                          |

### Standard pass template (sub-pass pattern, like SSAOPass)

```cpp
// Pass.h
class TK_API MyPass : public Pass
{
 public:
  MyPass();
  void Render() override;
  void PreRender() override;
  void PostRender() override;

  // Optional: populate m_requirements in PreRender, override the default
  // Gather() that the base class calls.
  void GatherRequirements(PassRequirements& reqs) override;

 private:
  // Sub-passes are usually a FullQuadPass per phase. Two fragment shaders ->
  // two quad instances, each pinned to its own program in the constructor.
  FullQuadPassPtr m_subPass     = nullptr;
  FullQuadPassPtr m_subPass2    = nullptr;
  ShaderPtr       m_shaderA     = nullptr;
  ShaderPtr       m_shaderB     = nullptr;
};

// Pass.cpp
MyPass::MyPass() : Pass("MyPass")
{
  m_shaderA = GetShaderManager()->Create<Shader>(ShaderPath("a.shader", true));
  m_shaderB = GetShaderManager()->Create<Shader>(ShaderPath("b.shader", true));

  // Pin each quad to its own shader ONCE in the constructor (renderer not alive
  // yet is fine -- SetFragmentShader only stages the program, BindProgram is no-op).
  // Or do this once in PreRender if the renderer must be live.
  m_subPass  = MakeNewPtr<FullQuadPass>();
  m_subPass2 = MakeNewPtr<FullQuadPass>();
}

void MyPass::Render()
{
  // Each phase: build requirements, Apply, draw.
  PassRequirements reqA;
  reqA.fragmentShader = m_shaderA;
  reqA.vertexShader   = m_subPass->m_material->GetVertexShaderVal();
  reqA.program        = m_subPass->GetProgram();
  reqA.frameBuffer    = m_outputFramebuffer;
  reqA.customUbos[7]  = &m_passDataBuffer.GetBuffer();
  reqA.textures[0]    = m_inputTexture;

  ApplyRequirements(GetRenderer());
  m_subPass->Render();
}

void MyPass::PreRender()
{
  Pass::PreRender();
  // Update UBO data, decide which sub-pass runs, etc.
  m_passDataBuffer.m_data.someField = ...;
  m_passDataBuffer.Invalidate();
  m_passDataBuffer.Map();
}
```

### Common mistakes

1. **Setting a framebuffer in the quad's `m_params.frameBuffer` instead of the
   outer pass's `m_requirements.frameBuffer`.** The outer pass owns the destination;
   the quad just consumes it. With the Gather fallback `reqs.frameBuffer = m_params.frameBuffer`,
   this leaks old framebuffer state from a prior sub-pass call.
2. **Forgetting `Map()` after writing UBO data.** `Map()` only uploads when
   `m_invalid == true`. Call `Invalidate()` first or your UBO silently keeps
   last frame's data.
3. **Re-using one `FullQuadPass` for two fragment shaders.** Drift bugs (e.g. the
   downsample shader lingering into an upsample draw) come from this. Use one quad
   per shader.
4. **Calling `renderer->BindUniformBuffer(slot, ub)` from `onPreRender` when the
   pass uses `ApplyRequirements`.** Set `m_requirements.customUbos[slot] = ub` instead
   and let Apply stage the descriptor set.
5. **Implicit sampler binding via material's diffuse texture when the shader binds a
   different sampler name.** When `BloomPass` lost `s_diffuseColor` to a default
   texture, it was because the quad material's diffuse texture was bound to slot 0
   independently of the explicit `SetTexture(0, srcTex)` call. Either set the
   material's diffuse to match (`SetDiffuseTextureVal(srcTex)`) or use a different
   sampler slot to avoid the collision.
6. **Driving a per-job stencil/depth prepass through `ApplyRequirements`.** The
   `StencilRenderPass` write phase renders `m_params.RenderJobs`, each of which
   carries its own `Material->GetProgram()` with its own uniforms. If you call
   `ApplyRequirements` with `m_requirements.program = m_program` (the solidOverride
   material's program), every job gets the wrong program and the wrong uniform
   layout -- visible symptom: outline goes missing on Vulkan because the outline
   pass's stencil-mask reads stale values. The write phase is one of the few places
   that stays on the older `renderer->SetPassState + renderer->Render(jobs)` flow.
   See "Exceptions to the declarative flow" below.

### When to override `GatherRequirements` vs. populate manually

- **Sub-class populates `m_requirements` in `PreRender()` or `Render()`** when the
  requirements are computed per-frame (e.g. camera-dependent state, dynamic UBOs).
- **Override `GatherRequirements`** when the same pass can be called from multiple
  places (e.g. `RunSubPass` in SSAOPass) and you want a single canonical builder.
  Use `if (reqs.fragmentShader == nullptr) reqs.fragmentShader = ...` so callers can
  still override fields by populating them first.

### When Apply is called

- Once at the top of `Render()`, before any draws.
- Inside `RenderSubPass(subPass)` (via `FullQuadPass::PreRender`), so nested passes
  get a fresh descriptor-set flush before their draw.
- NOT called from `RenderSubPass` for `Pass`-derived sub-passes that don't extend
  `FullQuadPass` -- those must call `ApplyRequirements` themselves.

### Exceptions to the declarative flow

The declarative `ApplyRequirements` flow assumes the pass draws with **one** program
that the pass owns. Two cases break this assumption and stay on the older imperative
flow:

1. **Per-job stencil / depth prepass** (e.g. `StencilRenderPass` write phase,
   hypothetical depth-prepass). The phase takes a `RenderJobArray` and lets
   `renderer->Render(jobs)` select the program per job from `job.Material`. Binding a
   single program via `ApplyRequirements` would shadow the per-job programs and break
   the entire draw.
   ```cpp
   // StencilRenderPass::Render -- known exception, do not migrate.
   renderer->SetPassState(m_writePassState);
   renderer->Render(*m_params.RenderJobs);
   ```
   Framebuffer + clear still come from `PreRender`'s `renderer->SetFramebuffer`.
   The post-write copy sub-phase (`m_copyStencilSubPass`) is a separate quad and
   uses the normal declarative flow.

2. **Passes that call `renderer->Render(jobs)` with `m_program` deliberately
   pre-bound** to set a non-default state (e.g. wireframe overlay) before per-job
   draws take over. Same problem as #1: if you set `m_requirements.program` to
   `m_program` you shadow the per-job programs. Only do the pre-bind in `PreRender`
   so the actual `Render()` stays imperative.

If you find a new third exception, document it here with the failing symptom.

---

## Object Lifetime & Shutdown Order

ToolKit owns every manager and the graphics backend inside the `Main` singleton. Host
applications tear it down in a fixed order, and **anything that holds an engine object
must be released before the host destroys the engine**. Getting this wrong does not fail
loudly at the point of the mistake: the offending object is destroyed later, during
`exit()`'s static destructors, and reaches into a `Main` that no longer exists.

The reported symptom of a violation is an abort on exit such as:

```
Main::GetInstance()   ToolKit.cpp    assert 'm_proxy && "ToolKit is not initialized."'
GetRenderSystem()     ToolKit.cpp
Mesh::UnInit          Mesh.cpp
Mesh::~Mesh           Mesh.cpp
std::_Sp_counted_ptr<Object*>::_M_dispose
__run_exit_handlers
```

### The teardown contract

```
App::Destroy            release application statics/caches holding engine objects
Main::PreUninit         engine still fully alive
Main::Uninit            managers release their resources, backend still alive
Main::PostUninit        destroys managers + RenderSystem, then sets m_proxy = nullptr
delete Main             ~Main asserts m_initiated == false
exit()                  static destructors run -- the engine is already gone here
```

`Main::PostUninit` nulls `m_proxy`, so from that point on `Main::GetInstance()` asserts by
design. `Main::PostUninit`'s own doc comment states the rule: *nothing is accessible from
this on*.

### Rules

1. **Never store engine objects in a static, global, or long lived cache.**
   Engine objects are `Object` derivatives (`Resource`, `Entity`, `Component`) plus the
   GPU types (`Framebuffer`, `UniformBuffer`, `GpuProgram`). A `shared_ptr` to any of them
   in a static keeps CPU and GPU state alive until process exit, which is after the engine
   is gone.

   ```cpp
   // WRONG: destroyed during exit(), after ToolKit is gone.
   void EditorViewport::HandleDrop()
   {
     static EntityPtr dwMesh = nullptr; // owns a MeshComponent, which owns a Mesh
   }

   // RIGHT: process lifetime state lives at file scope behind an accessor, so it can be
   // released on demand while the engine is still alive.
   namespace
   {
     struct DragDropCache { EntityPtr draggedMesh = nullptr; };
     DragDropCache& GetDragDropCache() { static DragDropCache cache; return cache; }
   }

   void EditorViewport::ReleaseDragDropState() { GetDragDropCache().draggedMesh = nullptr; }
   ```

   If the cached state genuinely has to outlive a single class instance, give it a
   `static void Release<Something>State()` entry point and call it from the host teardown.

2. **Prefer a member over a static.** Widget and window state (drag in progress, action
   being accumulated, pending selection) belongs to the class that draws it. Members die
   with the window, which is destroyed in `App::Destroy` / `DeleteWindows`, i.e. before
   `Main::Uninit`. A `static` in a member function is a process lifetime object no matter
   where it is written.

3. **Destructors reachable after `Main::PostUninit` must use the null safe accessors.**
   `Main::GetInstance_noexcep()`, `GetRenderSystem_noexcep()`, `GetBackend_noexcep()`,
   `GetAudioManager_noexcep()`, `GetHandleManager()` and `GetTKStats()` return `nullptr`
   once the engine is gone and must be checked. Skip GPU destruction in that case: the
   backend no longer exists, so there is nothing left to destroy.

   Reaching one of them after teardown is a contract violation, so it does not pass
   silently: `GetRenderSystem_noexcep()` and `GetAudioManager_noexcep()` raise
   `TK_ASSERT_ONCE`. Debug builds abort with a message; release builds log and continue,
   because a shipped game must not crash while exiting.

   ```cpp
   void Mesh::UnInit()
   {
     if (m_initiated)
     {
       // May run after the engine is gone (resource kept alive by an application cache).
       if (IGraphicsBackend* backend = GetBackend_noexcep())
       {
         backend->DestroyMesh(this);
       }
     }

     m_subMeshes.clear();
     m_initiated = false;
   }
   ```

   Hardened today: `Mesh::UnInit`, `Shader::UnInit`, `Texture::UnInit` and its overrides
   (`DepthTexture`, `DataTexture`, `CubeMap`, `Hdri`), `Framebuffer::UnInit`,
   `UniformBuffer::~UniformBuffer` / `UniformBuffer::Destroy`, `Audio::UnInit` (the
   miniaudio engine owns the sound data, so `ma_sound_uninit` is skipped once
   `AudioManager` is gone). `Object::~Object`, `GpuProgram::~GpuProgram` and
   `AnimRecord::~AnimRecord` already follow the pattern.

3b. **The same violation is also counted at its source.** `Main::PostUninit` reads
   `HandleManager::LiveObjectCount()` right after the render system is destroyed and reports a
   non zero result: it logs the count and raises `TK_ASSERT_ONCE`, so a leaking static is named
   even when the objects it holds never run a destructor. Debug builds also log the class
   distribution, which is what tells you where the leak lives:

   ```
   Main PostUninit: 7 engine objects outlived the engine. See AGENTS.md, ...
     live objects by class (total=7): Entity=1 Material=1 Mesh=1 MeshComponent=1 Shader=2 Texture=1
   ```

   The count is maintained by `Object::Object()` and `Object::~Object()`, which run exactly
   once per object. Do **not** move that bookkeeping into `Object::ParameterConstructor()`:
   a derived class may call it a second time on an already constructed object (see
   `EditorCamera::Copy()`), which counts one object twice and leaks the first generated handle.
   The debug class registry does live in `ParameterConstructor()`, because `Class()` still
   reports the base type while `Object::Object()` runs; it is keyed by address, so the repeat
   call cannot register the same object twice.

   That registry is reached through `TK_TRACK_LIVE_OBJECT()` and `TK_UNTRACK_LIVE_OBJECT()`.
   They are macros like `TK_ASSERT_ONCE`, not `if constexpr` statements, and that is not a
   style preference: `TK_DEBUG` is only defined by CMake for the Debug configuration, so
   `if constexpr (TK_DEBUG)` does not even compile in release, and a discarded `if constexpr`
   branch still has to name existing members -- while the point here is that
   `TrackObject` / `UntrackObject` / `DescribeLiveObjects` and the map and mutex behind them do
   not exist outside debug builds. Without `TK_DEBUG` the macros expand to `((void) 0)` and do
   not evaluate their arguments, so no `Class()` lookup is paid for in release.

   Class names in the dump follow the renames `ObjectFactory::Override()` applies. The editor
   renames `EditorCamera` to `Camera`, so an editor camera is reported as `Camera`.

   `TrackObject()` also asserts that the same address is not registered twice, which means
   `ParameterConstructor()` ran twice on one object and wasted the handle the first run
   generated. That is not theoretical: `EditorCamera::Copy()` re-ran it just to re-bind its
   `Poses` callback, and `DirectionalLight` copies a camera for every shadow cascade, so an
   editor session allocated hundreds of handles it never used. The guard is what found it and
   it stays as a regression check.

   For the same reason `HandleManager::m_uniqueIDs` is not a liveness metric. It also holds
   ids for `Node`, `Viewport`, `UILayer` and `AnimRecord`, which are not `Object`s, so a non
   zero handle set says nothing about live objects.

   Both hosts are measured clean, so any non zero report is a real leak: the editor and the
   Game template both reach `Main PostUninit` with 0 live objects.

   ```
   # Game template, real GL backend, normal exit
   Main Uninit -> Main PostUninit -> Main Destructed     (no "outlived" line)

   # Editor, normal exit
   Main Uninit -> Main PostUninit -> Main Destructed     (no "outlived" line)
   ```

4. **Keep the asserting accessors for creation, loading and render paths.**
   `GetRenderSystem()`, `GetMaterialManager()`, `GetMeshManager()` and friends must keep
   asserting there: a missing engine during `Init` / `Load` / `Render` is a real bug and
   must not be swallowed. Only destructor reachable cleanup uses the `_noexcep()` variants.

5. **Never hand a raw pointer to something you do not own into a static.**
   `FolderView`'s file operation state holds `DirectoryEntry*` into the `m_entries` of the
   folder views a `FolderWindow` owns; `FolderWindow::~FolderWindow` therefore calls
   `FolderView::ReleaseFileOperationState()`. Store an owning handle, an id, or a
   `weak_ptr` when the target can die first.

   An id counts as such a reference, because handle ids go back to the handle manager for
   reuse. `ViewportBase` releases `m_viewportId` and `UILayer` releases `m_id` in their
   destructors, so anything keyed by one of them has to be dropped first:
   `ViewportBase::~ViewportBase()` calls `UIManager::RemoveViewportLayers()` before it releases
   the id, otherwise a viewport that later reuses the id would inherit the layers of the one
   that is gone. When you start storing an id somewhere new, ask where it is erased.

6. **When a class-scope static table of engine objects is unavoidable** (the editor
   toolbar icon table in `UI::m_*Icon`, `UI::m_volatileWindows`), it MUST have a single
   documented release point that the host teardown calls while the engine is alive:
   `UI::UnInit()` for the icons, `App::DeleteWindows()` for the volatile windows,
   `EditorImGuiTextureCache::Clear()` for the Vulkan descriptor cache. Adding a new entry
   to such a table without extending its release point is a bug.

### Checklist for a new cache, static or host side manager

- Does the stored type transitively own an `Object` / GPU resource? Check nested types:
  `MeshComponent` owns a `Mesh`, `AnimRecord` owns an `Animation`, `TransformAction` owns
  an `Entity`, `GamePlugin` owns a `Viewport` (which owns a render target and a
  framebuffer).
- Can it be a member of the owning class instead?
- If it must be static: where is it released, and is that called from `App::Destroy`,
  `Main::PreUninit` or `Main::Uninit` -- in other words, while the backend is alive?
- If it is released from a destructor that can run after `PostUninit`: does every engine
  access in that path go through a `_noexcep()` accessor?

### Verification

A shutdown order regression is cheapest to catch with a headless harness:

```cpp
SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
RenderSystem::UseNullBackend();       // no window or GPU needed
Main* proxy = new Main();
Main::SetProxy(proxy);
proxy->PreInit();
proxy->Init();
// ... build the offending cache while the engine is alive ...
proxy->PreUninit();
proxy->Uninit();
proxy->PostUninit();
delete proxy;                        // m_proxy is now gone
// release the cache here: this must not abort
```

With the `NullBackend` the crash reproduces identically, because the failing assert is
about the missing `Main`, not about the backend.

---

## Documentation Maintenance (`gdtk-overview.md`)

`gdtk-overview.md` is the project's architectural / context file. It is the first
thing read at the start of every working session, so it MUST stay in sync with the
codebase. The agent maintaining the repo (human or AI) is responsible for updating
it after every meaningful change.

### When to update

Update `gdtk-overview.md` after a change that affects any of:

- **Project layout / solution structure** -- new folder, new vcxproj, file moved
  out of its old module, project renamed, new template added.
- **Build / dependency pipeline** -- new vendored dep, dep swapped, build script
  behavior change (e.g. `BuildScripts/build_dependencies.py` flow, output
  naming, generator flags), toolchain version bump, new compile define that
  changes the public surface (`-DTK_GL_ES_3_0` etc.).
- **Core engine architecture** -- new manager on the `Main` singleton, new
  subsystem, new render path, new pass, new UBO slot, new RHI backend, new
  resource type, new scene/ECS concept, new threading primitive.
- **Editor / tool surface** -- new editor window, new command, new plugin type,
  new project template, new import format, new packer mode.
- **Public API breakage or rename** -- class/function/file renamed or removed,
  serialization format version bump, behavior contract change for an existing
  API.
- **Section 14 (Quick File Lookup) drift** -- a header listed there moved or no
  longer exists, or a new key header is missing from the table.
- **Section 13 / Section 1 paths or facts** -- repo path, solution path, license,
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
2. Apply the minimal edit -- adjust the existing prose or table row, do not
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

---

## WSL / Linux Build

GDTK builds on Linux via WSL2. The build scripts (`BuildScripts/build_gdtk.py`
and `BuildScripts/build_dependencies.py`) handle both native Linux and WSL, but
WSL has one known pitfall: **Windows PATH leaks into the WSL session** and
CMake may find Windows binaries (e.g. `depot_tools/ninja`, a PE `.exe` wrapper
with CRLF line endings) before the Linux ones. The scripts now pass
`-DCMAKE_MAKE_PROGRAM=<resolved-ninja>` to every cmake configure call to pin
the correct ninja, but a clean PATH is still recommended.

### Prerequisites (Fedora)

```bash
# X11 dev headers (SDL2 video backend -- required)
sudo dnf install -y libX11-devel libXext-devel libXrandr-devel \
  libXcursor-devel libXi-devel libXinerama-devel libxkbcommon-devel

# Build tools
sudo dnf install -y cmake ninja-build gcc-c++ python3

# Optional: terminal emulator for showConsole testing
sudo dnf install -y xterm
```

### Clean build (recommended)

```bash
# From the GDTK root inside WSL:
rm -rf Dependency/Intermediate/Linux Intermediate/Linux

# Clean PATH -- strip Windows directories so CMake never sees host binaries.
# /usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin is the safe subset.
PATH='/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin' \
  python3 BuildScripts/build_gdtk.py --configs Debug --generator ninja
```

### Generator notes

| Generator | WSL safe?      | Notes |
|-----------|----------------|-------|
| `ninja`   | Yes (with fix) | Fastest. Scripts pin `-DCMAKE_MAKE_PROGRAM` so CMake uses the right one. |
| `make`    | Yes            | Slower but immune to the ninja PATH problem. Fallback if ninja acts up. |
| `auto`    | Caution        | Avoid on WSL -- `detect_generator` picks ninja but PATH may resolve the Windows wrapper. Pass `--generator ninja` or `--generator make` explicitly. |

### Quick incremental build (after first clean build)

```bash
PATH='/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin' \
  python3 BuildScripts/build_gdtk.py --configs Debug --generator ninja
```

The script skips deps if already built and only rebuilds changed sources.

### showConsole testing on WSL

The `SysComExec` `showConsole` path (see `ToolKit/Common/LinuxUtils.h`) wraps
commands in a terminal emulator window. WSLg provides `DISPLAY=:0` and
`WAYLAND_DISPLAY=wayland-0` automatically on Windows 11. Install a terminal
emulator that `ResolveTerminal()` knows about:

- `xterm` -- lightweight, always available (`sudo dnf install xterm`)
- `gnome-terminal`, `konsole`, `kitty`, `alacritty` -- also supported

The `ResolveTerminal()` function checks `$TERMINAL` first, then probes the
common emulators in order (see `LinuxUtils.h` for the full list).
Without any emulator installed, `showConsole` falls back to running the
command hidden -- the same as `showConsole=false`.
