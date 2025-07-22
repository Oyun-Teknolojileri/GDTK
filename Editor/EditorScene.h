/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorTypes.h"

#include <Scene.h>

namespace ToolKit
{
  namespace Editor
  {

    // EditorScene
    //////////////////////////////////////////

    class TK_EDITOR_API EditorScene : public Scene
    {
     public:
      TKDeclareClass(EditorScene, Scene);

      EditorScene();
      virtual ~EditorScene();

      void Load() override;
      void Update(float deltaTime) override;

      // Selection operations.
      //////////////////////////////////////////

      /** Returns the selected entities in the scene. */
      bool IsSelected(ObjectId id) const;

      /** Remove the entity with the given id from selection list. */
      void RemoveFromSelection(ObjectId id);

      void AddToSelection(const IDArray& entities, bool additive);
      void AddToSelection(const EntityPtrArray& entities, bool additive);
      void AddToSelection(ObjectId id, bool additive);
      void ClearSelection();
      bool IsCurrentSelection(ObjectId id) const;

      /** Makes the entity current selection.  */
      void MakeCurrentSelection(ObjectId id);

      uint GetSelectedEntityCount() const;
      EntityPtr GetCurrentSelection() const;

      // Resource operations
      //////////////////////////////////////////
      void Save(bool onlyIfDirty) override;

      // Entity operations.
      //////////////////////////////////////////

      void AddEntity(EntityPtr entity, int index = -1) override;
      void RemoveEntity(const EntityPtrArray& entities, bool deep = true) override;

      /**
       * remove entity from the scene
       * @param  the id of the entity you want to remove
       * @param  do you want to remove with children ?
       *         be aware that removed children transforms preserved
       * @returns the entity that you removed, nullptr if entity is not in scene
       */
      EntityPtr RemoveEntity(ObjectId id, bool deep = true) override;

      void Destroy(bool removeResources) override;
      void GetSelectedEntities(EntityPtrArray& entities) const;
      void GetSelectedEntities(IDArray& entities) const;
      void SelectByTag(const String& tag);

      // Pick operations
      PickData PickObject(const Ray& ray,
                          const IDArray& ignoreList       = {},
                          const EntityPtrArray& extraList = {}) override;

      void PickObject(const Frustum& frustum,
                      PickDataArray& pickedObjects,
                      const IDArray& ignoreList       = {},
                      const EntityPtrArray& extraList = {}) override;

      // Gizmo operations
      //////////////////////////////////////////

      /** Checks if a gizmo addable to the entity, and adds a gizmo for it if so. */
      void AddBillboard(EntityPtr entity);

      /** If there is an associated gizmo with the entity, removes the gizmo. */
      void RemoveBillboard(EntityPtr entity);

      /** Returns all entity billboards in the scene. */
      EntityPtrArray GetBillboards();
      EntityPtr GetBillboard(EntityPtr entity);

      /**
       * Makes sure the entity has right type of billboard.
       * Update or change the billboard type based on components if necessary.
       */
      void ValidateBillboard(EntityPtr entity);

      /** Performs billboard validation for an array of entities. */
      void ValidateBillboard(EntityPtrArray& entities);

     private:
      void CopyTo(Resource* other) override;

      /**
       * Updates the billboards to align with current viewports camera for
       * proper picking.
       */
      void UpdateBillboardsForPicking();

      /** Internally used to sanitize selection before adding it. */
      void AddToSelectionSane(ObjectId id);

     public:
      /**
       * Indicates whether the scene is saved to disk or not. If true its a new scene. Not yet saved.
       * It may have a file but not yet saved to that file.
       */
      bool m_newScene;

     private:
      /** Holds the selected entities in the scene. */
      IDArray m_selectedEntities;

      /** Holds a map for matching billboards with owner entities. */
      std::unordered_map<EntityPtr, EditorBillboardPtr> m_entityBillboardMap;

      /** Holds all entity billboards in the scene. */
      EntityPtrArray m_billboards;
    };

    // EditorSceneManager
    //////////////////////////////////////////

    class TK_EDITOR_API EditorSceneManager : public SceneManager
    {
     public:
      EditorSceneManager();
      virtual ~EditorSceneManager();
    };

  } // namespace Editor
} // namespace ToolKit
