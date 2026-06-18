# GDTK - Project Overview (Starting Point)

> This file is the FIRST thing to read at the start of every GDTK working session.
> It captures the high-level architecture so we can dive straight into specific tasks.
>
> Repo path: `C:\Users\Cihan\Desktop\GDTK`
> Solution: `C:\Users\Cihan\Desktop\GDTK\ToolKit.sln`
> C++ style: see `~/.mavis/memory/programming-rules.md` (`m_mymember`, `MyLittleFunction()`, `/** */`, `mystructmember`, **always** curly braces).

---

## 1. What GDTK Is

GDTK (Game Development ToolKit) is a **3D editor + interactive application platform** built by OtSoftware (Turkish company). License: LGPL-3.0 with a commercial option.

Philosophy: minimal, modular, lightweight, bloat-free — alternative to Unity/Unreal for indie devs and researchers.

- Cross-platform: Windows, Linux, Mac (Windows is primary)
- Publish targets: Windows `.exe`, Web `.html + .wasm/.js`, Android `.apk`
- C++17-ish, CMake + Visual Studio 2022, Ninja
- Editor uses ImGui, runtime is engine-agnostic UI

---

## 2. Solution Layout (`ToolKit.sln`)

```
GDTK/
├── Engine/                      # Engine filter (ToolKit.vcxproj)
│   ├── ToolKit/                 # Core engine (ToolKit.vcxproj) — most of the code lives here
│   │   ├── Common/              # Cross-platform utils (UTF-8, base64, SDL event pool, Win32, splash)
│   │   ├── Vulkan/              # Vulkan backend (VulkanBackend, Shader, Swapchain, Pipeline cache, etc.)
│   │   ├── x64/                 # Build output
│   │   └── *.h / *.cpp          # Headers at root for fast discovery
│   ├── Editor/                  # Editor.vcxproj — ImGui-based 3D editor application
│   └── ...
├── Utils/
│   ├── Import/                  # Asset import (assimp-based, into engine format)
│   ├── Packer/                  # Resource packer (.tk files)
│   └── ...
├── Modules/
│   └── Workspace/               # Project workspace management
├── Templates/
│   ├── Plugin/                  # Plugin template (Plugin.vcxproj)
│   └── Game/                    # Game template (Game.vcxproj)
├── Launcher/                    # Launcher.vcxproj — picks up the engine + project
├── Dependency/                  # Vendored deps (assimp, glm, glad, imgui, etc.)
├── Resources/                   # Engine resources (shaders, textures, default scenes)
├── Config/                      # Default engine config files
├── Bin/                         # Compiled editor.exe + runtime files
├── Templates/                   # Project templates
├── CMakeLists.txt               # Top-level CMake
├── BuildScripts/                # Cross-platform build scripts
└── .clang-format                # Project formatting rules
```

---

## 3. Engine Core Architecture

### 3.1 `Main` Singleton (ToolKit.h)

The single global access point for every manager. Created by host (Editor or Launcher), initialized via `PreInit -> Init -> App::Init -> PostInit`.

Owns all managers:

```
Main
├── m_animationMan        AnimationManager
├── m_animationPlayer     AnimationPlayer
├── m_audioMan            AudioManager
├── m_materialManager     MaterialManager
├── m_meshMan             MeshManager
├── m_shaderMan           ShaderManager
├── m_spriteSheetMan      SpriteSheetManager
├── m_textureMan          TextureManager
├── m_sceneManager        SceneManager
├── m_pluginManager       PluginManager
├── m_logger              Logger
├── m_uiManager           UIManager
├── m_skeletonManager     SkeletonManager
├── m_fileManager         FileManager
├── m_objectFactory       ObjectFactory
├── m_renderSys           RenderSystem
├── m_engineSettings      EngineSettings
├── m_tkStats             TKStats
├── m_workerManager       WorkerManager          (thread pool)
├── m_gpuBuffers          GlobalGpuBuffers        (UBO bundle)
├── m_handleManager       HandleManager           (unique ObjectId generator, thread-safe)
├── m_timing              Timing                  (delta time, FPS lock)
├── m_eventPool           EventPool               (event bus)
├── m_threaded            bool                    (toggles all threading)
└── m_pre/m_postUpdateFns vector<TKUpdateFn>      (per-frame callbacks)
```

Frame loop: `FrameBegin -> FrameUpdate -> FrameEnd`. `FrameUpdate` fires `m_preUpdateFns`, runs `Frame(dt)`, fires `m_postUpdateFns`.

Free accessors in `ToolKit.h`: `GetLogger()`, `GetRenderSystem()`, `GetMaterialManager()`, etc.

### 3.2 EngineSettings (EngineSettings.h)

Serializable, loaded from `%appdata%/ToolKit/Config`. Sub-objects:

- **WindowSettings**: name, width, height, fullscreen
- **GraphicSettings**: FPS, MSAA, HDR, MultiThreaded, RenderResolutionScale, AnisotropicTextureFiltering, plus a `m_shadows` member
- **ShadowSettings** (nested in GraphicSettings): cascade count/distance, parallel split, stable shadow map, atlas resolution (1K/2K), PCF kernel, VSM blur
- **PostProcessingSettings**: tonemapping, bloom, gamma, SSAO, DOF, FXAA
- **ShaderSettings**: per-shader define presets (e.g. "Low"/"High" graphics presets) so shader compile doesn't explode combinatorially

`ShadowSettings::ParameterEventConstructor` is the hook that pushes the values into the graphics constant buffer via `ValueUpdateFn`.

---

## 4. Rendering Subsystem

### 4.1 RenderSystem (RenderSystem.h)

- Owns the `IGraphicsBackend*` (OpenGL or Vulkan).
- Maintains **two render task queues** — `m_highQueue` and `m_lowQueue` — and `ExecuteRenderTasks()` drains them in order. Tasks are pushed via `AddRenderTask(RenderTask{ Task, Callback, Priority })`.
- Frame pacing: `StartFrame()`, `EndFrame()`, `Present()`.
- Hosts the `GpuProgramManager*` (cache of `GpuProgram` = vertex+fragment pair, deduped by stage-shader-id hash).
- `SetPresentCallback` lets the host (Editor) hook into present for screen capture etc.
- Has a `m_skipFrames` counter for skipping scene updates during modal operations.

### 4.2 RenderPath (RenderSystem.h)

Base class. A list of `PassPtrArray` that runs `PreRender` -> each pass's `Render()` -> `PostRender`.

Subclasses:
- `ForwardSceneRenderPath` — used in the **editor scene viewport** and is the heavyweight path with shadows/SSAO/bloom/DoF/tonemap.
- `GameRenderer` — extends `RenderPath`, used in **runtime game rendering**. Wraps a `SceneRenderPathPtr` for scene, then a UI pass, then gamma/tonemap/fxaa pass.

### 4.3 ForwardSceneRenderPath (ForwardSceneRenderPath.h)

The main rendering pipeline. Holds:

```
ForwardSceneRenderPath
├── m_shadowPass                ShadowPass
├── m_forwardPreProcessPass     ForwardPreProcessPass
├── m_forwardRenderPass         ForwardRenderPass
├── m_skyPass                   CubeMapPass
├── m_ssaoPass                  SSAOPass
├── m_bloomPass                 BloomPass
├── m_dofPass                   DoFPass
├── m_gammaTonemapFxaaPass      GammaTonemapFxaaPass
└── m_resolvedFramebuffer       FramebufferPtr (for MSAA resolve)
```

Inputs via `SceneRenderPathParams`: Scene, Camera, MainFramebuffer, grid, postProcessSettings, optional `overrideLights`.

### 4.4 Pass (Pass.h)

Base render pass. Holds a `GpuProgramPtr m_program` and a `StringView m_name` (RenderDoc label).

`RenderSubPass(pass)` for sub-passes (e.g. blur chains).

#### PassRequirements (declarative draw binding)

Passes declare what they need for a draw via a `PassRequirements` struct:

```cpp
struct PassRequirements
{
  ShaderPtr fragmentShader;
  ShaderPtr vertexShader;
  GpuProgramPtr program;          // optional pre-built program
  FramebufferPtr frameBuffer;
  GraphicBitFields clearBits;
  std::unordered_map<int, TexturePtr>    textures;           // slot-indexed
  std::unordered_map<String, TexturePtr> semanticTextures;   // name-indexed, resolved AFTER program bind
  std::unordered_map<int, UniformBuffer*> customUbos;         // pass-specific UBOs (slot 7 in particular)
  std::unordered_map<String, String>     defines;
  RenderState passState;
  bool scissorEnabled; UVec4 scissor;
};
```

`Pass::ApplyRequirements(renderer)` walks this in a fixed 7-step order:
1. Shader defines -> recompile fragment shader.
2. Build or reuse `GpuProgramPtr` from vert+frag (fails loudly if either is missing).
3. Bind the program.
4. Bind the framebuffer + clear bits.
5. Apply the passive RenderState (depth test/write, stencil, blend override, etc.).
6. Stage scissor (if enabled).
7. Bind custom UBOs by slot -> bind textures by slot -> bind textures by semantic name.

This deterministic order replaces the older "call `renderer->SetTexture` /
`SetFramebuffer` / `BindProgram` from inside the pass body" pattern. The old pattern
leaked descriptor-set bindings between passes on Vulkan (slot 7 in particular is
shared by Bloom, DoF, SSAO, gamma, outline, gradient sky, grid); ApplyRequirements
makes the binding deterministic per draw.

#### Sub-class flow

```cpp
class MyPass : public Pass {
  void Render() override {
    PassRequirements req;
    req.fragmentShader = m_shader;
    req.vertexShader   = m_subPass->m_material->GetVertexShaderVal();
    req.program        = m_subPass->GetProgram();
    req.frameBuffer    = m_output;
    req.customUbos[7]  = &m_ubo.GetBuffer();
    req.textures[0]    = m_input;
    ApplyRequirements(GetRenderer());   // 7-step bind
    m_subPass->Render();               // pure draw call
  }
};
```

See `AGENTS.md` section "Pass / PassRequirements Conventions" for the full pattern,
including the two-quad rule for passes that use two fragment shaders (Bloom, etc.).

#### RenderJob & RenderData

#### RenderJob & RenderData

`RenderJob` = one drawcall's worth of data:
- `Entity*`, `Mesh*`, `Material*`
- `EnvironmentComponent* EnvironmentVolume` + `SecondaryEnvironmentVolume` (IBL blend)
- `bool ShadowCaster, frustumCulled, requireCullFlip`
- `BoundingBox`, `WorldTransform`, `AnimData`
- `LightRawPtrArray lights` (lights assigned to this job)

`RenderData` = partitioned job list:
```
[Culled | Deferred Opaque | Deferred AlphaMasked | Forward Opaque | Forward AlphaMasked | Forward Translucent]
                ^                    ^                    ^                      ^                       ^
                |                    |                    |                      |                       |
deferredJobsStartIndex   deferredAlphaMaskedJobsStartIndex   forwardOpaqueStartIndex  forwardAlphaMaskedJobsStartIndex  forwardTranslucentStartIndex
```

`RenderJobProcessor` provides: `CreateRenderJobs`, `SeperateRenderData`, `AssignLight`, `AssignEnvironment`, `PreSortLights` (puts directional lights first), `SortByDistanceToCamera`, `SortByMaterial`, `CalculateStdev`/`IsOutlier`.

### 4.5 Renderer (Renderer.h)

Heavyweight class that drives the GPU. Key responsibilities:

- **Backend ownership** via `SetBackend(IGraphicsBackend*)`. Backend lifetime transfers in.
- **Camera binding** (`SetCamera`) — also caches the `CameraCacheItem` and computes view/projection/inverse on the GPU side.
- **Light binding** (`SetLights`, `SetDirectionalLights`) — point + spot bound per-object via index lists, directional lights set once per pass.
- **Material/program binding** (`SetMaterial`, `BindProgramOfMaterial`).
- **Framebuffer ops** (`SetFramebuffer`, `ResolveFramebuffer` for MSAA, `CopyFrameBuffer`, `CopyTexture`).
- **Draw** (`Render(job)`, `Render(jobs)`, `RenderWithProgramFromMaterial`).
- **Cubemap utilities** (`GenerateCubemapFrom2DTexture`, `GenerateEquiRectengularProjection`, `CopyCubeMapToMipLevel`, `GenerateSpecularEnvMap`, `GenerateDiffuseEnvMap`, `RenderToCubeMap`).
- **Gaussian blur helpers** (`ApplyGaussianBlur`, `ApplyGaussianBlurToArrayLayerSlot` — for shadow atlas slot blur).
- **BRDF LUT** generation (`GenerateBRDFLutTexture`).
- **Per-draw UBO** (`SubmitPerDrawData` to backend slot 6 — currently mostly empty, in active migration).
- **Timer queries** (`StartTimerQuery`/`EndTimerQuery`/`GetElapsedTime` for CPU/GPU profiling).

#### Renderer global UBO layout

All bind at fixed slots (slot 5 = pass UBOs, slot 6 = per-draw UBO):

| Layout | Slot | Used by |
|---|---|---|
| `CameraGpuBuffer` | global | every pass |
| `GraphicConstantsGpuBuffer` | global | every pass (shadow distance, atlas size, IBL lod, cascades) |
| `DirectionalLightBuffer` | global | forward pass |
| `PointLightCache` / `SpotLightCache` | global | forward pass |
| `PerDrawUboBuffer` | 6 | per-draw (model matrix etc., mirror in `perDrawDataInc.shader`) |
| `DilatePassDataLayout` | 5 | outline/dilate pass |
| `GammaTonemapFxaaPassDataLayout` | 5 | gamma/tonemap/fxaa pass |
| `BloomPassDataLayout` | 5 | bloom downsample/upsample chain |
| `GaussBlurPassDataLayout` | 5 | shadow blur, gaussian passes |
| `CubemapEquirectPassDataLayout` | 5 | equirect -> cubemap conversion |
| `PreFilterEnvMapPassDataLayout` | 5 | specular IBL prefilter |
| `GridPassDataLayout` | 5 | editor grid |
| `SsaoBlurPassDataLayout` | 5 | SSAO blur |
| `DofPassDataLayout` | 5 | DoF pass |
| `GradientSkyboxPassDataLayout` | 5 | gradient skybox |
| `SsaoCalcPassDataLayout` | 5 | SSAO calc |

> **Std140 mirroring rule**: every `*Layout` struct in C++ MUST match its GLSL counterpart byte-for-byte. If you add a field, add it in the matching `.shader` include in the same order.

#### DrawCommand (Renderer.h)

The big per-object uniform block. Carries:
- `global0/1` — flags (IBL in use, AO in use, sky intensity, light counts)
- Two **environment volumes** (primary + secondary for IBL blending), each with 11 `Vec4`:
  - `vol[0|1]Params` (intensity, fadeDistance, interior, pccEnabled)
  - `vol[0|1]Min/Max` (AABB in local space)
  - `vol[0|1]InvT[0..3]` (inverse world transform)
  - `vol[0|1]WldT[0..3]` (world transform)

---

## 5. Graphics Backends

### 5.1 `IGraphicsBackend` (IGraphicsBackend.h)

Pure-virtual interface that all RHI calls funnel through. Both GL and Vulkan implement this.

Core API:
- Lifecycle: `InitBackend`, `BeginFrame`, `EndFrame`, `Present`
- Pass ops: `StartPass(PassDesc)`, `FinishPass`, `SetViewport`, `SetScissor`, `ClearBuffer`, `ClearColorBuffer`
- Draw: `BindPipeline`, `SubmitPerDrawData`, `BindTexture(slot, tex)`, `BindUniformBuffer(name, ub)`, `Draw(DrawDesc)`
- FB ops: `ResolveFramebuffer`, `CopyFramebuffer`, `BlitToScreen`
- Queries: `StartTimerQuery`, `EndTimerQuery`, `GetElapsedTime`
- Texture mgmt: `CreateTexture`, `DestroyTexture`, `ApplyTextureSettings`, `GenerateMipmaps`, `UpdateTextureRegion`, `SetTextureMaxMipLevel`, `AllocateCubemapMipStorage`, `CopyCubemapFaceFromFramebuffer`, `SetTextureSwizzleAlpha`
- Mesh mgmt: `CreateMesh`, `DestroyMesh`

`PassDesc` = `{ target framebuffer, clearBits, discardBits, clearColor, loadColor, loadDepth }`
`DrawDesc` = `{ mesh, vertexLayout, indexed, elementCount, instanceCount, drawType }`
`BackendInitParams` = `{ getProcAddress, windowHandle, errorCallback, vkInstanceExtensions, vkCreateSurface }` — both backends, the editor fills platform-specific bits.

### 5.2 OpenGL Backend (GLBackend.h)
- Header: `ToolKit/GLBackend.h`
- Defined in `TK_GL_ES_3_0` global define (set in top-level CMake)
- Uses `TKOpenGL.h` + `GlErrorReporter`

### 5.3 Vulkan Backend (Vulkan/ directory)
- `VulkanBackend.h` (main implementation)
- `VulkanBindings.h` (binding marshalling)
- `VulkanBuffer.h`
- `VulkanContext.h` (instance, device, queues)
- `VulkanDescriptor.h` (descriptor sets)
- `VulkanPipelineCache.h`
- `VulkanResources.h`
- `VulkanShader.h` (SPIR-V)
- `VulkanSwapchain.h`

**Important note from the code**: Editor fills `vkInstanceExtensions` and `vkCreateSurface` callbacks — ToolKit stays SDL-free. Editor uses `SDL_Vulkan_GetInstanceExtensions` and `SDL_Vulkan_CreateSurface`.

### 5.4 RHI Constants (RHI.h)
Single header for cross-backend constants (`TextureSlotCount`, `MaxPointLightPerObject`, `MaxSpotLightPerObject`, `MultiChoiceVariant`, `VertexLayout`, `DrawType`, `BlendFunction`, `MsaaSampleCount`, `GraphicBitFields`, `ShadingMode`).

---

## 6. Resources

### 6.1 Resource (Resource.h)

Base class for every loadable asset. Inherits Object. Has lifecycle: `Load()` (CPU memory), `Init()` (GPU memory), `UnInit()`, `Reload()`, `Save(onlyIfDirty)`. Has `m_dirty`/`m_loaded`/`m_initiated` flags and `m_file`/`_missingFile` (the latter is set when a file is missing and a default is used — prevents overwriting the original file with default content on save).

`SerializeRef` / `DeserializeRef` write/read a `<ResourceRef Type=... File=... />` node.

### 6.2 ResourceManager (ResourceManager.h)

`unordered_map<String, ResourcePtr> m_storage` keyed by file path. Template `Create<T>(file)` checks for file existence, falls back to `GetDefaultResource` if missing (logs warning). `Copy<T>(source)` for in-memory clones. `Remove(file)` returns the resource.

`GetResourceManager(ClassMeta*)` returns the right manager for a given class — there's one per resource type: `MeshManager`, `TextureManager`, `MaterialManager`, `ShaderManager`, `AnimationManager`, `SkeletonManager`, `AudioManager`, `SpriteSheetManager`, `SceneManager`, `PluginManager`.

### 6.3 Concrete resources

- **Mesh** (Mesh.h): `Vertex{pos, norm, tex, tan}` (tangent.w is bitangent sign), `Face{Vertex* v[3]}`, indices, AABB, optional skinning data. `Init` uploads to GPU via backend's `CreateMesh`.
- **Material** (Material.h): holds `VertexShader` + `FragmentShader` (`ShaderPtr`), references to textures, `RenderState`, `MaterialCacheItem::Data` (std140: colorAlpha, emissiveThreshold, metallicRoughness, textureFlags). Implements `ICacheable` (cached data uploaded via `GetCacheItem()`).
- **Shader** (Shader.h): supports `VertexShader`, `FragmentShader`, `IncludeShader`. Has `ShaderDefine` + `ShaderDefineArray` for variants. Master vs include shader concept: master defines the entry + variant defines; include has no entry. Each shader has `ShaderResourceArray m_resources` declaring texture/UBO bindings with `ViewType` (`Tex2D`, `Tex2DArray`, `TexCube`) — used by Vulkan to pick the right dummy texture for unbound slots.
- **Texture** (Texture.h), **CubeMap** (Texture.h subclass), **RenderTarget** (offscreen target), **Audio**, **Animation** (`Animation.h` + `AnimationControllerComponent`), **Skeleton** (`Skeleton.h` + `SkeletonComponent`), **SpriteSheet**, **Prefab**, **Plugin**.

### 6.4 ObjectFactory (ObjectFactory.h)
Reflection: given a `ClassMeta*`, creates instances. Used for serialization round-trips.

---

## 7. Scene & ECS

### 7.1 Entity (Entity.h)

The "thing in the scene". Inherits `Object`. Components attached for behavior:
- `MeshComponent` (visible mesh)
- `MaterialComponent`
- `AnimationControllerComponent`
- `SkeletonComponent`
- `EnvironmentComponent` (IBL volume)
- `DirectionComponent` (directional light, billboard etc.)
- `AABBOverrideComponent`
- `EditorLight`, `EditorCamera`, `EditorBillboard` (editor-only)
- `EditorEnvironmentComponent` (editor-only)

Has `ObjectPtr Copy()`, `RemoveResources()`, `Parent()`, `IsDrawable()`, `SetPose(anim, time)`, `GetBoundingBox(inWorld)`, `IsVisible()`, `SetVisibility(vis, deep)`.

### 7.2 Node (Node.h)
Transform / hierarchy. `m_inheritScale`, `m_inheritTranslate`, `m_inheritRotate`. `Parent()` traverses up.

### 7.3 Component (Component.h)
Base component. Each `Component` belongs to an `Entity`. Parameter system (`TKDeclareParam`) for serialization.

### 7.4 Scene (Scene.h)

`Resource` subclass. Holds `m_entities` (EntityPtrArray) + BVH (`AABBTree m_aabbTree`).

Key operations:
- Add/remove entities (with deep flag for hierarchy)
- Picking: `PickObject(Ray, ignoreList, extraList)` returns `PickData{pickPos, entity}`; also a frustum version returning `PickDataArray`.
- Queries: `GetEntity`, `GetFirstByName`, `GetByTag`, `GetFirstByTag`, `Filter(predicate)`.
- Lights/sky/env caching: `GetLights`, `GetDirectionalLights`, `GetSky`, `GetEnvironmentVolumes`.
- Serialization: `SerializeImp` / `DeSerializeImp` to XML; `DeSerializeImpV045` for legacy v0.4.5 files.
- Prefab support: `LinkPrefab`, `SavePrefab`.
- Update loop: `Update(deltaTime)`.

Caches maintained in `UpdateEntityCaches`:
- `m_lightCache`, `m_directionalLightCache`
- `m_environmentVolumeCache`
- `m_skyCache`
- `m_entityIdToIndex` (rebuilt by `_RebuildEntityIdMap` after bulk operations)

Per-scene: `m_postProcessSettings` (overrides engine's settings).
`SceneManager` (subclass of ResourceManager) tracks the current scene.

---

## 8. Camera, Viewport, Renderer

### 8.1 Camera (Camera.h)
Standard projection camera. Fov, near, far, aspect, orthographic, is2D. `SetLens` for aspect adjust.

### 8.2 Viewport (Viewport.h)

`ViewportBase`: holds a `CameraPtr`. Has `m_viewportId` (unique ID) and an `m_attachedCamera` ObjectId.
`Viewport` extends with `m_framebuffer` (the FB that viewport renders into) and `m_size`.

`GameViewport` is the runtime viewport (`Update(dt)`).

`EditorViewport` (Editor/) + `EditorViewport2d` are editor versions with gizmos, picking, etc.

`SwapCamera` for swapping attached camera (preserves ID).

### 8.3 GameRenderer (GameRenderer.h)

Used by **runtime game apps** (not editor). Pipeline:
1. `SceneRenderPathPtr` (a `ForwardSceneRenderPath`)
2. `ForwardRenderPassPtr m_uiPass` (the game's UI, with `m_uiRenderJobs` / `m_uiRenderData`)
3. `GammaTonemapFxaaPassPtr m_gammaPass`

Params: `viewport`, `scene`, `postProcessSettings`.

`EditorRenderer` (Editor/) is the editor's renderer with gizmo passes (`GizmoPass`).

---

## 9. Editor Architecture

### 9.1 App (Editor/App.h)

The editor application. Owns `EditorScenePtr m_scene` (current scene), the `EditorRenderer`, the `Workspace`, `SimulationWindow` (PIE — Play-In-Editor), `DynamicMenu`, thumbnail system.

Key entry points:
- `Init() / Destroy() / Frame(dt) / OnResize`
- `OnNewScene`, `OnSaveScene`, `OnSaveAsScene`, `OnQuit`
- `OnNewProject`, `OnNewPlugin(name)`
- `SetGameMod(GameMod mod)` — switch between edit and play mode
- `CompilePlugin(name, gamePlugin, async)` — invoke the C++ compiler on a plugin
- `LoadGamePlugin`, `LoadProjectPlugins`
- `Import(fullPath, subDir, overwrite)` — via assimp
- `OpenSceneAsync`, `MergeScene`, `LinkScene`
- `OpenProject(Project)`, `PackResources`, `SaveAllResources`
- `ExecSysCommand(cmd, async, showConsole, callback)` — wraps `system()` with utf-8 + completion callback
- `IsWorkspaceSane`, `IsValidCppLibraryName`

`ClearSession(flushRenderTasks)` — must be called between project switches or when stopping PIE.
`ClearPlayInEditorSession()` — only PIE-created objects.

### 9.2 Workspace (Editor/ via `Workspace.h` project)
Manages the user's project directory inside the editor's app data. Stores project list, last opened project, workspace root.

### 9.3 Editor windows
All ImGui-based: `OutlinerWindow`, `FolderWindow` (asset browser), `ConsoleWindow`, `MaterialView`, `MeshView`, `EntityView`, `ComponentView`, `EngineSettingsWindow`, `MultiChoiceWindow`, `Anchor` / `AnchorMod`, `BoxEditGizmo` / `BoxEditMod` (transform gizmos), `ConsoleWindow`, `FolderWindow`, `Gizmo`, `Grid` (editor grid), `LightMeshGenerator`, `OverlayLighting`, `AndroidBuildWindow`, `CustomDataView`, `Thumbnail`, `SimulationWindow`.

### 9.4 EditorRenderer (Editor/EditorRenderer.h)
Editor version of `Renderer` path. Adds `GizmoPass` and editor grid pass. Multiple viewports (4-up default).

### 9.5 EditorScene (Editor/EditorScene.h)
Subclass of `Scene` with editor-specific additions (e.g. handles entity picking + gizmo update each frame).

---

## 10. Threading (Threads.h)

### 10.1 WorkerManager

Three executors:
- `MainThread` — runs at end of current frame, in `WorkerManager::ExecuteTasks` tick (sync with main).
- `FramePool` — for tasks that **must** complete in the current frame (uses `m_frameWorkers` thread pool).
- `BackgroundPool` — for long-running async work (uses `m_backgroundWorkers` thread pool).

`AsyncTask(Executor, F, args...)` returns a `std::future<R>`. If `Main::m_threaded == false`, falls back to MainThread regardless of choice.

Built on `task_thread_pool` (vendored `poolSTL/include/poolstl/poolstl.hpp`).

### 10.2 Macros

```cpp
TKExecBy(WorkerManager::FramePool)            // parallel for_each with FramePool
TKExecByConditional(cond, FramePool)          // parallel only when both `cond` and `m_threaded`
TKAsyncTask(BackgroundPool, Func, arg1, ...)  // fire-and-forget async
```

### 10.3 Synchronization primitives

- `Spinlock` (rigtorp's), `SpinlockGuard` (RAII), `SpinWaitBarrier(cond)` — for low-contention quick locks.
- `HyperThreadPause()` macro: `_mm_pause` on MSVC, `__builtin_ia32_pause` on GCC, `yield` on ARM, no-op on Emscripten.

### 10.4 HandleManager
`GenerateHandle()` is thread-safe via `Spinlock m_uniqueIdWriteLock`. Returns random `ObjectId` from `m_randomXor[2]` seed; recycled on `ReleaseHandle`.

### 10.5 RenderSystem
`AddRenderTask` enqueues; `ExecuteRenderTasks` drains high queue then low queue. Tasks can be `High` or `Low` priority. `RenderTask::Callback` fires after.

---

## 11. Packer / Import / Launcher

### 11.1 Packer (Packer/Packer.vcxproj)
Builds `.tk` packs from editor resources for shipping. CLI tool.

### 11.2 Import (Import/Import.vcxproj)
Asset import (assimp-based). Editor uses it via `App::Import`.

### 11.3 Launcher (Launcher/Launcher.vcxproj)
Project launcher — picks up the engine + project, runs the game.

---

## 12. Build & Run

### 12.1 Build deps
```bash
cd /path/to/GDTK
python3 BuildScripts/build_dependencies.py
```

Use `--configs Debug Release` (default), `--skip-assimp` / `--skip-imgui` to
drop deps you do not need right now, and `--clean` to wipe
`Dependency/Intermediate/<Platform>/` before configuring. The script is
cross-platform: on Windows it picks `Ninja` if available, otherwise
`Visual Studio 17 2022`; on Linux it picks `Ninja` or `Unix Makefiles`.

The dependency build produces **flat** output names for assimp — `assimp.lib` /
`assimpd.lib` and `assimp.dll` / `assimpd.dll` (Debug vs Release split is via
`CMAKE_DEBUG_POSTFIX=d`). This is enforced from the wrapper in
`Dependency/CMakeLists.txt` (overrides `assimp` target's `OUTPUT_NAME` /
`ARCHIVE/LIBRARY/RUNTIME_OUTPUT_NAME` after `add_subdirectory`); assimp's own
CMake is NOT modified. The flat name is what `Import/Import.vcxproj` links
against (`assimp.lib` / `assimpd.lib` in `AdditionalDependencies`), and it is
stable across MSVC toolsets (vc142 / vc143 / vc145 all produce the same name).
Upstream assimp's compiler-suffixed name (`assimp-vc143-mt.lib`) is no longer
emitted into the dependency output directory.

### 12.2 Open in Visual Studio
```bat
start ToolKit.sln
```
Set **Editor** as the startup project. **Debug|x64** or **Release|x64**. **Build -> Build Solution** (Ctrl+Shift+B).

### 12.3 First run
Editor asks for a workspace dir (e.g. `C:\Users\Cihan\Documents\TK-Workspace`). It writes to `%appdata%/ToolKit/Config/`. Delete that folder to reset.

### 12.4 Create project
Menu bar -> New project (name must be ASCII alphanumeric, no whitespace).

### 12.5 In project
VSCode opens `Workspace/Project/Codes` with template code. `F7` in VSCode compiles, "Build" button in editor also compiles, "Play" runs simulation in editor, attach VSCode to ToolKit for debugging.

### 12.6 CMake
Top-level `CMakeLists.txt` requires CMake >= 3.6. `add_definitions(-DTK_GL_ES_3_0 -DTK_DLL_EXPORT)`. `add_subdirectory(${TOOLKIT_DIR}/ToolKit ...)`. The `TOOLKIT_DIR` env var / cache var points to the engine module.

---

## 13. Coding Style Reminders (cross-ref: ~/.mavis/memory/programming-rules.md)

When writing/editing any `.h`/`.cpp` in this repo:

- Comments and identifiers: **English only**, **ASCII only** (no Turkish, no emoji). If I find non-ASCII I MUST fix it and tell you.
- Members: `m_mymember` (lowercase, no underscore prefix aside from the leading `m_`).
- Functions: `MyLittleFunction()`.
- Struct members: `mystructmember` (lowercase, no `m_` prefix).
- Comments: `/** doc */` doxygen style for declarations.
- `if/for/while`: **always** open a brace on the next line. **Never** write a one-liner.
- No code duplication — if I see the same logic twice, I refactor.
- C++ standard observed in deps: C++11/14 (assimp draco = cxx_std_11, googletest = cxx_std_14).

---

## 14. Quick File Lookup

| What | File |
|---|---|
| Global access | `ToolKit/ToolKit.h` |
| Engine settings | `ToolKit/EngineSettings.h` |
| Render system | `ToolKit/RenderSystem.h` |
| Renderer | `ToolKit/Renderer.h` |
| Pass base | `ToolKit/Pass.h` |
| Forward scene path | `ToolKit/ForwardSceneRenderPath.h` |
| Game renderer | `ToolKit/GameRenderer.h` |
| Backend iface | `ToolKit/IGraphicsBackend.h` |
| GL backend | `ToolKit/GLBackend.h` |
| Vulkan backend | `ToolKit/Vulkan/VulkanBackend.h` |
| Resource base | `ToolKit/Resource.h` |
| Resource manager | `ToolKit/ResourceManager.h` |
| Mesh | `ToolKit/Mesh.h` |
| Material | `ToolKit/Material.h` |
| Shader | `ToolKit/Shader.h` |
| GpuProgram | `ToolKit/GpuProgram.h` |
| Framebuffer | `ToolKit/Framebuffer.h` |
| Viewport | `ToolKit/Viewport.h` |
| Entity | `ToolKit/Entity.h` |
| Node | `ToolKit/Node.h` |
| Component | `ToolKit/Component.h` |
| Scene | `ToolKit/Scene.h` |
| Threads / Worker | `ToolKit/Threads.h` |
| Editor app | `Editor/App.h` |
| Editor renderer | `Editor/EditorRenderer.h` |
| Workspace | `Workspace/Workspace.vcxproj` |
| Build deps | `BuildScripts/build_dependencies.py` |
| Dep wrapper (assimp name fix, output dirs) | `Dependency/CMakeLists.txt` |
| Solution | `ToolKit.sln` |

---

## 15. First Reads in Any New Session

Before changing anything, open these in order:
1. `ToolKit/ToolKit.h` — manager layout (the `Main` singleton is the map of the engine).
2. The relevant subsystem header for the task (e.g. `RenderSystem.h`, `Material.h`, `Scene.h`).
3. The corresponding `.cpp` in the same folder (use `Get-ChildItem -Filter *.cpp` if the impl file is named differently from the header).
4. If touching the editor: `Editor/App.h` + the relevant `Editor/Editor*.h`.
5. If touching shaders: matching `.shader` file in `Resources/Engine/Shaders/`.
