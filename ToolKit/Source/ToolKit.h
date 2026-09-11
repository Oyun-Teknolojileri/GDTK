/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file ToolKit.h Header for Main, the manager class to access all the
 * functionalities of the ToolKit framework.
 */

#include "Logger.h"
#include "Platform.h"
#include "Threads.h"
#include "Types.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

/**
 * Base name space for all the ToolKit functionalities.
 */
namespace ToolKit
{
  /**
   * Callback for registering to frame updates.
   * Use Main::RegisterPreUpdateFunction and Main::RegisterPostUpdateFunction for receiving updates.
   */
  typedef std::function<void(float deltaTime)> TKUpdateFn;

  class Object;

  /**
   * Provides unique ids and keeps the record of the live engine objects.
   *
   * The two jobs are split on cost, not on subject. Id generation is needed in every build
   * (the id is serialized), and the live object count is a plain atomic that pays for itself as
   * a teardown diagnostic. The class distribution behind Main::PostUninit's leak report needs a
   * mutex, a heap node and a String copy per object, and exists in debug builds only: the
   * counter tells a shipped game that it leaked, the registry tells a developer what leaked.
   *
   * The set of assigned ids is not a liveness metric even so: Node, Viewport, UILayer and
   * AnimRecord take ids without being Objects.
   */
  class TK_API ObjectRegistry
  {
   public:
    ObjectRegistry(); //!< Default constructor, initializes the registry with a random seed.

    /**
     * Random id that guarantees uniqueness on runtime. Collisions are resolved during deserialize, if any.
     * These ids, freed when using of it completed. So ids are reused and do not overflow.
     */
    ObjectId GenerateId();

    void RegisterId(ObjectId val);    //!< Record the id, preventing it from being acquired again.
    void ReleaseId(ObjectId val);     //!< Free the id for reuse.
    bool IsIdAvailable(ObjectId val); //!< Test if the id is free.

    /**
     * Records that an Object instance became alive and drops that record again in the
     * destructor. The count is separate from the id set on purpose: ids are also taken by non
     * Object types (Node, Viewport, UILayer, AnimRecord), so the id set can not be used to tell
     * whether engine objects are still alive.
     */
    void ObjectCreated();

    /** Drops the record of the object. Debug builds also erase it from the leak report registry. */
    void ObjectDestroyed(const Object* object);

    /**
     * Number of live Object instances. Main::PostUninit asserts on it to verify that no engine
     * object outlived the engine.
     */
    uint64 LiveObjectCount() const;

#ifdef TK_DEBUG
    /**
     * Debug only bookkeeping behind Main::PostUninit's leak report, so a violation names the
     * classes that were left behind instead of only counting them.
     *
     * Keyed by address, which is stable for the lifetime of an object. Ids can not be used:
     * deserialization reassigns them, so the id an object was created with is not the id it is
     * destroyed with.
     */
    void TrackObject(const Object* object, const String& className);

    /** Debug only: class distribution of the objects that are still alive. */
    String DescribeLiveObjects() const;
#endif

   private:
    ObjectId m_randomXor[2];                  //!< Random seed.
    std::unordered_set<ObjectId> m_uniqueIds; //!< Container for all acquired ids.
    Spinlock m_uniqueIdWriteLock;             //!< Guaranties thread safety for modifying the registry's state.

    /**
     * Live Object instances, see ObjectCreated(). Relaxed atomic on purpose: it is a diagnostic
     * counter, and objects are created from worker threads. It is the only part of the live
     * object bookkeeping that survives into release builds.
     */
    std::atomic<uint64> m_liveObjectCount {0};

#ifdef TK_DEBUG
    mutable std::mutex m_liveObjectLock;                     //!< Guards m_liveObjects.
    std::unordered_map<const Object*, String> m_liveObjects; //!< Live objects and their class. See TrackObject().
#endif
  };

  /**
   * Structure that holds time related data.
   * Main has one of this which gives current application time.
   */
  struct TK_API Timing
  {
    /** Initialize the timer for the engine. Locks the engine to given frame per seconds. */
    void Init(uint fps);

    /** Returns the elapsed time for the last frame in milliseconds. */
    float GetDeltaTime();

    float CurrentTime     = 0.0f; //!< Total elapsed time in milliseconds. Updated after every frame.
    float TargetDeltaTime = 0.0f; //!< Target delta time in milliseconds calculated as (1000 / Target Fps)
    int FramesPerSecond   = 0;    //!< Number of frames drawn within 1 second.
    int FrameCount        = 0;    //!< Internally used to count number of frames per second.
    float LastTime        = 0.0f; //!< Internally used to determine if enough time has passed for a new frame.
    float TimeAccum       = 0.0f; //!< Internally used to determine if enough time has passed for a new frame.
  };

  /**
   * Main class that provides access to all sorts of manager and utility functionalities provided by the Engine.
   */
  class TK_API Main
  {
   public:
    /**
     * Default constructor. Does not initialize the Main.
     */
    Main();

    /**
     * Default destructor. Does not uninitialie the main.
     */
    virtual ~Main();

    Main(const Main&)           = delete;
    void operator=(const Main&) = delete;

    virtual void PreInit(); //!< Creates all the managers and systems for the engine.
    virtual void Init();    //!< Initialize all the managers and systems. Engine fully functions at this point.

    /**
     * Systems that requires engine will be initialized at this stage.
     * Expected run time order is:
     * Main::PreInit, Main::Init, App::Init, Main::PostInit
     */
    virtual void PostInit();

    /**
     * Systems that requires engine and the application will be uninitialized at this stage.
     * Expected run time order is:
     * Main::PreUninit, App::Uninit, Main::Uninit, Main::PostUninit.
     */
    virtual void PreUninit();

    virtual void Uninit();     //!< Uninitialize all the managers and systems.
    virtual void PostUninit(); //!< Destroy all the engine allocated resources. Nothing is accessible from this on.

    /**
     * Overrides the default configPath, if not changed, its relative to editor.exe. Default: ../Config
     * @param cfgPath is the new path for default config files. For windows it could be %appdata%/ToolKit.
     */
    void SetConfigPath(StringView cfgPath);

    /**
     * Overrides the default resource path, if not changed, its relative to editor.exe. Default: ../Resources/Engine
     * It will only be effective if set before PreInit.
     * @param path is the new path for default resource files.
     */
    void SetDefaultPath(StringView path);

    /**
     * Returns config path if set. It won't return any default path in case if
     * nothing is set. To access the proper config path see ConfigPath().
     */
    StringView GetConfigPath();

    static Main* GetInstance();         //!< Access function for the instance of Main.
    static Main* GetInstance_noexcep(); //!< Access function for the instance of Main. Does not perform debug checks.
    static void SetProxy(Main* proxy);  //!< Sets the instance of Main that will be used afterwards.

    /**
     * This function should be called at the beginning of frame.
     */
    void FrameBegin();

    /**
     * This function updates data that ToolKit handles.
     * This function also calls registered PreUpdate and PostUpdate functions.
     */
    void FrameUpdate();

    /**
     * This function should be called at the end of frame.
     */
    void FrameEnd();

    /**
     * This function registers function that should be called before ToolKit update every frame.
     */
    void RegisterPreUpdateFunction(TKUpdateFn preUpdateFn);

    /**
     * This function registers function that should be called after ToolKit update every frame.
     */
    void RegisterPostUpdateFunction(TKUpdateFn postUpdateFn);

    /**
     * This function clears registered pre-update functions.
     */
    void ClearPreUpdateFunctions();

    /**
     * This function clears registered post-update functions.
     */
    void ClearPostUpdateFunctions();

    /**
     * @return Current frame count per second.
     */
    int GetCurrentFPS() const;

    /**
     * @return Total elapsed time in millisecond since the initialization.
     */
    float TimeSinceStartup() const;

    /**
     * @return true if enough time have passed from previous frame
     */
    bool SyncFrameTime();

   private:
    void Frame(float deltaTime); //!< Performs an update for all engine components.

   public:
    Timing m_timing; //!< Timer that keeps time related data since the Initialization.
    class AnimationManager* m_animationMan     = nullptr;
    class AnimationPlayer* m_animationPlayer   = nullptr;
    class AudioManager* m_audioMan             = nullptr;
    class MaterialManager* m_materialManager   = nullptr;
    class MeshManager* m_meshMan               = nullptr;
    class ShaderManager* m_shaderMan           = nullptr;
    class SpriteSheetManager* m_spriteSheetMan = nullptr;
    class TextureManager* m_textureMan         = nullptr;
    class SceneManager* m_sceneManager         = nullptr;
    class PluginManager* m_pluginManager       = nullptr;
    class Logger* m_logger                     = nullptr;
    class UIManager* m_uiManager               = nullptr;
    class SkeletonManager* m_skeletonManager   = nullptr;
    class FileManager* m_fileManager           = nullptr;
    class ObjectFactory* m_objectFactory       = nullptr;
    class RenderSystem* m_renderSys            = nullptr;
    class EngineSettings* m_engineSettings     = nullptr;
    class TKStats* m_tkStats                   = nullptr;
    class WorkerManager* m_workerManager       = nullptr;
    struct GlobalGpuBuffers* m_gpuBuffers      = nullptr;
    ObjectRegistry m_objectRegistry;

    bool m_preInitiated = false;
    bool m_initiated    = false;
    bool m_threaded     = true;
    String m_resourceRoot;
    String m_defaultResourceRoot;
    String m_cfgPath;
    EventPool m_eventPool;

   private:
    static Main* m_proxy;

    std::vector<TKUpdateFn> m_preUpdateFunctions;
    std::vector<TKUpdateFn> m_postUpdateFunctions;
  };

  // Accessors.
  TK_API class Logger* GetLogger();
  TK_API class RenderSystem* GetRenderSystem();

  /**
   * Null safe variant of GetRenderSystem(). Returns nullptr once the engine is gone.
   * Destructors that can run after Main::PostUninit (resources kept alive by application
   * statics, globals or caches) must use this instead of the asserting accessor, because
   * the backend is already destroyed at that point and there is nothing left to release.
   */
  TK_API class RenderSystem* GetRenderSystem_noexcep();

  /**
   * Returns the active graphics backend, or nullptr when the engine is already gone.
   * Convenience wrapper around GetRenderSystem_noexcep() for GPU teardown paths.
   */
  TK_API class IGraphicsBackend* GetBackend_noexcep();

  TK_API class AnimationManager* GetAnimationManager();
  TK_API class AnimationPlayer* GetAnimationPlayer();
  TK_API class AudioManager* GetAudioManager();

  /** Null safe variant of GetAudioManager(). See GetRenderSystem_noexcep(). */
  TK_API class AudioManager* GetAudioManager_noexcep();

  TK_API class MaterialManager* GetMaterialManager();
  TK_API class MeshManager* GetMeshManager();
  TK_API class ShaderManager* GetShaderManager();
  TK_API class SpriteSheetManager* GetSpriteSheetManager();
  TK_API class TextureManager* GetTextureManager();
  TK_API class SceneManager* GetSceneManager();
  TK_API class PluginManager* GetPluginManager();
  TK_API class UIManager* GetUIManager();
  TK_API class ObjectRegistry* GetObjectRegistry();
  TK_API class SkeletonManager* GetSkeletonManager();
  TK_API class FileManager* GetFileManager();
  TK_API class EngineSettings& GetEngineSettings();
  TK_API class ObjectFactory* GetObjectFactory();
  TK_API class TKStats* GetTKStats();
  TK_API class WorkerManager* GetWorkerManager();
  TK_API Timing* GetTiming();

  // Path.
  TK_API String DefaultPath();
  TK_API String ConfigPath();
  TK_API String EngineSettingsPath();
  TK_API String ResourcePath(bool def = false);
  TK_API String ResourceParentPath(bool def = false);
  TK_API String TexturePath(const String& file, bool def = false);
  TK_API String MeshPath(const String& file, bool def = false);
  TK_API String FontPath(const String& file, bool def = false);
  TK_API String SpritePath(const String& file, bool def = false);
  TK_API String AudioPath(const String& file, bool def = false);
  TK_API String AnimationPath(const String& file, bool def = false);
  TK_API String SkeletonPath(const String& file, bool def = false);
  TK_API String ShaderPath(const String& file, bool def = false);
  TK_API String MaterialPath(const String& file, bool def = false);
  TK_API String ScenePath(const String& file, bool def = false);
  TK_API String PrefabPath(const String& file, bool def = false);
  TK_API String LayerPath(const String& file, bool def = false);
  TK_API String PluginPath(const String& file, bool def = false);
  TK_API String PluginConfigPath(const String& file, bool def = false);

} // namespace ToolKit
