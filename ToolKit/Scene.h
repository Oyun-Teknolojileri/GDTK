/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

/**
 * @file Scene.h Header file for the Scene class.
 */

#include "AABBTree.h"
#include "EngineSettings.h"
#include "EnvironmentComponent.h"
#include "Resource.h"
#include "Sky.h"
#include "Types.h"

namespace ToolKit
{
  /**
   * The Scene class represents a collection of entities in a 3D environment. It
   * provides functionality for loading and saving scenes, updating and querying
   * entities, and serializing / deserializing to / from XML.
   */
  class TK_API Scene : public Resource
  {
   public:
    TKDeclareClass(Scene, Resource);

    /** A helper struct that holds the result of a ray-picking operation in the scene. */
    struct PickData
    {
      /** The position in world space where the ray intersects with the object. */
      Vec3 pickPos;

      /** A pointer to the Entity object that was picked. */
      EntityPtr entity = nullptr;
    };

    typedef std::vector<PickData> PickDataArray;

   public:
    /** The constructor for the Scene class. */
    Scene();

    /** The destructor for the Scene class. */
    virtual ~Scene();

    /** Constructs scene and ToolKit objects associated with scene.*/
    virtual void NativeConstruct() override;

    /** Constructs scene, sets file and creates ToolKit objects associated with scene.*/
    virtual void NativeConstruct(const String& file);

    /** Loads the scene from its file. */
    void Load() override;

    /** Returns true if this scene is created as a 2D layer. */
    bool IsLayerScene();

    /**
     * Saves the scene to its file.
     *
     * @param onlyIfDirty If true, the scene will only be saved if it has been
     * modified since the last save.
     */
    void Save(bool onlyIfDirty) override;

    /**
     * Initializes the scene.
     *
     * @param flushClientSideArray It will pass down to resource components of
     * the entity for initialization.
     */
    void Init(bool flushClientSideArray = false) override;

    /** Uninitializes the scene. */
    void UnInit() override;

    /**
     * Updates the scene by the specified amount of time.
     * @param deltaTime The time elapsed since the last update.
     */
    virtual void Update(float deltaTime);

    /**
     * Merges the entities from another scene into this scene and clears the other scene.
     * @param other A pointer to the other scene to merge.
     */
    virtual void Merge(ScenePtr other);

    // Scene queries.

    /**
     * Performs a ray-picking operation on the scene to find the first object
     * that the ray intersects.
     *
     * @param ray The ray to use for picking.
     * @param ignoreList A list of entity IDs to ignore during the picking
     * operation.
     * @param extraList A list of extra entity pointers to include in the
     * picking operation.
     *
     * @return A PickData struct containing the result of the picking operation.
     */
    virtual PickData PickObject(const Ray& ray, const IDArray& ignoreList = {}, const EntityPtrArray& extraList = {});

    /**
     * Performs a frustum culling operation on the scene to find all objects
     * that are partially or fully contained within the frustum.
     *
     * @param frustum The frustum to use for culling.
     * @param pickedObjects An output vector to store the results of the culling
     * operation.
     * @param ignoreList A list of entity IDs to ignore during the culling
     * operation.
     * @param extraList A list of extra entity pointers to include in the
     * culling operation.
     */
    virtual void PickObject(const Frustum& frustum,
                            PickDataArray& pickedObjects,
                            const IDArray& ignoreList       = {},
                            const EntityPtrArray& extraList = {});

    /**
     * Gets the entity with the given ID from the scene.
     * @param id The ID of the entity to get.
     * @param index is the index in to the entity list. If provided index of the entity is filled.
     * @returns The entity with the given ID, or nullptr if no entity with that
     * ID exists in the scene.
     */
    EntityPtr GetEntity(ObjectId id, int* index = nullptr) const;

    /** Adds an entity to the scene. If an index is provided, insert the entity to the given position in the array. */
    virtual void AddEntity(EntityPtr entity, int index = -1);

    /** Adds an array of entities to the scene. */
    virtual void AddEntity(const EntityPtrArray& entities);

    /**
     * Gets all the entities in the scene.
     * @returns An array containing pointers to all the entities in the scene.
     */
    const EntityPtrArray& GetEntities() const;

    /** Returns all lights in the scene including directional lights.  */
    const LightRawPtrArray& GetLights() const;

    /** Returns all directional lights in the scene. */
    const LightRawPtrArray& GetDirectionalLights() const;

    /**
     * Gets the sky object associated with the scene. If multiple exist, returns the last one.
     * @returns Last added sky entity or null if not exist.
     */
    SkyBasePtr& GetSky();

    /**
     * Returns an array of pointers to all environment volume components in the scene.
     * @returns The array of pointers to environment volume components.
     */
    EnvironmentComponentPtrArray& GetEnvironmentVolumes() const;

    /**
     * Gets the first entity in the scene with the given name.
     * @param name The name of the entity to get.
     * @returns The first entity in the scene with the given name, or nullptr if
     * no entity with that name exists in the scene.
     */
    EntityPtr GetFirstByName(const String& name);

    /**
     * Gets an array of all the entities in the scene with the given tag.
     * @param tag The tag to search for.
     * @returns An array containing pointers to all the entities in the scene
     * with the given tag.
     */
    EntityPtrArray GetByTag(const String& tag);

    /**
     * Gets the first entity in the scene with the given tag.
     * @param tag The tag to search for.
     * @returns The first entity in the scene with the given tag, or nullptr if
     * no entity with that tag exists in the scene.
     */
    EntityPtr GetFirstByTag(const String& tag);

    /**
     * Filters the entities in the scene using the given filter function.
     * @param filter A function that takes an Entity pointer as its argument and
     * returns a boolean value indicating whether to include the entity in the
     * filtered result.
     * @returns An array containing pointers to all the entities in the scene
     * that passed the filter function.
     */
    EntityPtrArray Filter(std::function<bool(EntityPtr)> filter);

    /**
     * Links a prefab to the scene.
     * @param fullPath The full path to the prefab file.
     */
    void LinkPrefab(const String& fullPath);

    /**
     * Removes the entity with the given ID from the scene.
     *
     * This function will remove the specified entity from the scene.
     * If the `deep` parameter is set to true, it will also remove all
     * child entities associated with the specified entity.
     *
     * @param id The ID of the entity that will be removed.
     * @param deep States if the remove will be recursive to all leafs.
     * @returns The removed entity, or nullptr if no entity with that ID exists.
     */
    virtual EntityPtr RemoveEntity(ObjectId id, bool deep = true);
    virtual EntityPtr RemoveEntity(EntityPtr entity, bool deep = true);

    /**
     * Removes an array of entities from the scene.
     * @param entities An array of pointers to the entities to be removed.
     * @param deep States if the remove will be recursive to the all leafs.
     */
    virtual void RemoveEntity(const EntityPtrArray& entities, bool deep = true);

    /** Removes all entities from the scene. */
    virtual void RemoveAllEntities();

    /**
     * Destroys the scene and removes all of its resources.
     * @param removeResources Whether to remove all associated resources or not.
     */
    virtual void Destroy(bool removeResources);

    /**
     * Saves a prefab for an entity in the scene.
     * @param entity The entity to create the prefab from.
     * @param name is the name of the prefab for the saved file.
     * @param sub folder that the prefab will be saved in prefabs folder.
     */
    virtual void SavePrefab(EntityPtr entity, String name, String path);

    /** Removes all entities from the scene. */
    virtual void ClearEntities();

    /** Returns scene boundary from the BVH. */
    const BoundingBox& GetSceneBoundary();

   protected:
    /**
     * Serializes the scene to an XML document.
     * @param doc The XML document to serialize to.
     * @param parent The parent XML node to serialize under.
     */
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;

    /** Counts the number of objects node in the xml to provide a loading progress measure. */
    void PreDeserializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

    /**
     * Deserializes the scene from an XML document.
     * @param doc The XML document to deserialize from.
     * @param parent The parent XML node to deserialize from.
     */
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

    /** Deserialize files with version v0.4.5 */
    void DeSerializeImpV045(const SerializationFileInfo& info, XmlNode* parent);

    /**
     * Returns the biggest number generated during the current runtime. This
     * function is used to avoid ID collision during scene merges.
     *
     * @return The biggest entity ID generated during the current runtime.
     */
    ObjectId GetBiggestEntityId();

    /**
     * Copies the scene to another resource.
     * @param other The resource to copy to.
     */
    void CopyTo(Resource* other) override;

    /**
     * Updates all entity caches based on the entity type and components.
     * Adds or removes the entity / component to the caches.
     */
    void UpdateEntityCaches(const EntityPtr& ntt, bool add);

   private:
    /**
     * Internally used only.
     * Intention is to remove children of this entity to aid remove deep operation.
     * To preserve hierarchy, the parent / child relation is preserved.
     * That is, when query the children list of this entity, you'll see removed entities.
     * @param removed The entity whose children will be removed.
     */
    void _RemoveChildren(EntityPtr removed);

   public:
    PostProcessingSettingsPtr m_postProcessSettings; //!< Post process settings that this scene uses

    /** Bounding box binary tree that encapsulates all entities for this scene. */
    AABBTree m_aabbTree;

   protected:
    EntityPtrArray m_entities; //!< The entities in the scene.
    bool m_isPrefab;           //!< Whether or not the scene is a prefab.
    bool m_isLayer;            //!< Whether or not the scene is a 2D layer.

    mutable LightRawPtrArray m_lightCache;                         //!< Cached light entities which is added to scene.
    mutable LightRawPtrArray m_directionalLightCache;              //!< Cached directional lights in the scene.
    mutable EnvironmentComponentPtrArray m_environmentVolumeCache; //!< Environment volumes in the scene.
    mutable SkyBasePtr m_skyCache;                                 //!< Last added sky.
  };

  /**
   * A class for managing game scenes as resources.
   */
  class TK_API SceneManager : public ResourceManager
  {
   public:
    /**
     * Constructor.
     */
    SceneManager();

    /**
     * Destructor.
     */
    virtual ~SceneManager();

    /**
     * Initializes the scene manager.
     */
    void Init() override;

    /**
     * Uninitializes the scene manager.
     */
    void Uninit() override;

    /**
     * Checks if a given resource type can be stored by the scene manager.
     * @param t The resource type to check.
     * @return True if the resource type can be stored, false otherwise.
     */
    bool CanStore(ClassMeta* Class) override;

    /**
     * Gets the default resource file path for the given resource type.
     * @param type The resource type.
     * @return The default resource file path.
     */
    String GetDefaultResource(ClassMeta* Class) override;

    /**
     * Gets the currently active scene.
     * @return A pointer to the current scene.
     */
    ScenePtr GetCurrentScene();

    /**
     * Sets the currently active scene.
     * @param scene A pointer to the new current scene.
     */
    void SetCurrentScene(const ScenePtr& scene);

   private:
    ScenePtr m_currentScene; //!< A pointer to the currently active scene.
  };

} // namespace ToolKit
