/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "EditorScene.h"

#include "Action.h"
#include "App.h"
#include "EditorViewport.h"
#include "OutlinerWindow.h"
#include "Prefab.h"

namespace ToolKit
{
  namespace Editor
  {

    // EditorScene
    //////////////////////////////////////////

    TKDefineClass(EditorScene, Scene);

    EditorScene::EditorScene() { m_newScene = true; }

    EditorScene::~EditorScene() { Destroy(false); }

    void EditorScene::Load()
    {
      m_newScene = false; // Loaded scene's can't be new.

      // Load the scene.
      Scene::Load();

      // Adjust editor entities.
      for (EntityPtr& ntt : m_entities)
      {
        // Create gizmos
        if (ntt->IsA<DirectionalLight>())
        {
          ntt->As<EditorDirectionalLight>()->InitController();
        }
        else if (ntt->IsA<PointLight>())
        {
          ntt->As<EditorPointLight>()->InitController();
        }
        else if (ntt->IsA<SpotLight>())
        {
          ntt->As<EditorSpotLight>()->InitController();
        }
      }
    }

    void EditorScene::Update(float deltaTime)
    {
      Super::Update(deltaTime);

      // Show selected light gizmos.
      EntityPtrArray selecteds;
      GetSelectedEntities(selecteds);

      LightRawPtrArray sceneLights = GetLights();

      bool foundFirstLight         = false;
      Light* firstSelectedLight    = nullptr;
      for (Light* light : sceneLights)
      {
        bool found = false;
        for (EntityPtr ntt : selecteds)
        {
          if (light->GetIdVal() == ntt->GetIdVal())
          {
            if (!foundFirstLight)
            {
              firstSelectedLight = light;
              foundFirstLight    = true;
            }
            EnableLightGizmo(light, true);
            found = true;
            break;
          }
        }

        if (!found)
        {
          EnableLightGizmo(light, false);
        }
      }

      // Update billboards attached to entities.
      for (EntityPtr billboard : m_billboards)
      {
        BillboardPtr bb     = Cast<Billboard>(billboard);
        bb->m_worldLocation = bb->m_entity->m_node->GetTranslation();
      }
    }

    bool EditorScene::IsSelected(ObjectId id) const
    {
      return std::find(m_selectedEntities.begin(), m_selectedEntities.end(), id) != m_selectedEntities.end();
    }

    void EditorScene::RemoveFromSelection(ObjectId id)
    {
      auto nttIt = std::find(m_selectedEntities.begin(), m_selectedEntities.end(), id);
      if (nttIt != m_selectedEntities.end())
      {
        m_selectedEntities.erase(nttIt);
      }
    }

    void EditorScene::AddToSelection(ObjectId id, bool additive)
    {
      if (!additive)
      {
        m_selectedEntities.clear();
      }

      if (IsSelected(id))
      {
        if (!additive) // we've cleared the array, must add again
        {
          AddToSelectionSane(id);
        }
        return;
      }

      EntityPtr ntt = GetEntity(id);
      bool skipAdd  = false; // Prefab sub entities may cause addition of the root.

      // If the entity comes from a prefab scene, swap the child ntt with prefab.
      if (Entity* prefabRoot = ntt->GetPrefabRoot())
      {
        ntt     = prefabRoot->Self<Entity>();
        id      = ntt->GetIdVal();
        skipAdd = IsSelected(id);
      }

      if (!skipAdd)
      {
        AddToSelectionSane(id);
      }

      // Execute commands that activates on selection.
      // A better approach would be creating an editor event OnSelected.
      if (GetApp()->m_selectEffectingLights && !ntt->IsA<Light>())
      {
        LightRawPtrArray lights = GetLights();
        for (Light* light : lights)
        {
          light->UpdateShadowCamera();
        }

        RenderJobArray jobs;
        EntityRawPtrArray entities = {ntt.get()};

        int dirEnd                 = RenderJobProcessor::PreSortLights(lights);
        RenderJobProcessor::CreateRenderJobs(jobs, entities, false, dirEnd, lights);

        if (!jobs.empty())
        {
          RenderJob& job = jobs.front();

          for (size_t i = 0; i < job.lights.size(); i++)
          {
            Light* light = job.lights[i];
            if (!IsSelected(light->GetIdVal()))
            {
              AddToSelection(light->GetIdVal(), true);
            }
          }
        }
      }
    }

    void EditorScene::AddToSelection(const IDArray& entities, bool additive)
    {
      ObjectId currentId = NullHandle;
      for (const ObjectId& id : entities)
      {
        if (IsCurrentSelection(id))
        {
          currentId = id;
        }
      }

      // Start with a clear selection list.
      if (!additive)
      {
        ClearSelection();
      }

      for (const ObjectId& id : entities)
      {
        // Add to selection.
        if (!additive)
        {
          AddToSelection(id, true);
        }
        else // Add, make current or toggle selection.
        {
          if (IsSelected(id))
          {
            if (GetSelectedEntityCount() > 1)
            {
              if (entities.size() == 1)
              {
                if (IsCurrentSelection(id))
                {
                  // Deselect if user is reselecting the current selection.
                  RemoveFromSelection(id);
                  if (id == currentId)
                  {
                    currentId = NullHandle;
                  }
                }
                else
                {
                  // Make it current.
                  MakeCurrentSelection(id);
                }
              }
            }
            else
            {
              RemoveFromSelection(id);
              if (id == currentId)
              {
                currentId = NullHandle;
              }
            }
          }
          else
          {
            AddToSelection(id, true);
          }
        }
      }

      if (currentId != NullHandle)
      {
        MakeCurrentSelection(currentId);
      }
    }

    void EditorScene::AddToSelection(const EntityPtrArray& entities, bool additive)
    {
      IDArray ids = ToEntityIdArray(entities);
      AddToSelection(ids, additive);
    }

    void EditorScene::ClearSelection() { m_selectedEntities.clear(); }

    bool EditorScene::IsCurrentSelection(ObjectId id) const
    {
      if (!m_selectedEntities.empty())
      {
        return m_selectedEntities.back() == id;
      }

      return false;
    }

    void EditorScene::MakeCurrentSelection(ObjectId id)
    {
      IDArray::iterator itr = std::find(m_selectedEntities.begin(), m_selectedEntities.end(), id);
      if (itr != m_selectedEntities.end())
      {
        // If the id exist, swap it to the last to make it current selection.
        std::iter_swap(itr, m_selectedEntities.end() - 1);
      }
      else
      {
        // Entity is not selected, adding it back of the array makes it both selected and current.
        AddToSelectionSane(id);
      }
    }

    uint EditorScene::GetSelectedEntityCount() const { return (uint) m_selectedEntities.size(); }

    EntityPtr EditorScene::GetCurrentSelection() const
    {
      if (!m_selectedEntities.empty())
      {
        return GetEntity(m_selectedEntities.back());
      }

      return nullptr;
    }

    void EditorScene::Save(bool onlyIfDirty)
    {
      Scene::Save(onlyIfDirty);
      m_newScene = false;
    }

    void EditorScene::AddEntity(EntityPtr entity, int index)
    {
      Scene::AddEntity(entity, index);

      // Add billboard gizmo.
      AddBillboard(entity);
    }

    void EditorScene::RemoveEntity(const EntityPtrArray& entities, bool deep)
    {
      Scene::RemoveEntity(entities, deep);

      for (EntityPtr ntt : entities)
      {
        RemoveFromSelection(ntt->GetIdVal());
        RemoveBillboard(ntt);
      }
    }

    EntityPtr EditorScene::RemoveEntity(ObjectId id, bool deep)
    {
      EntityPtr removed = nullptr;
      if (removed = Scene::RemoveEntity(id, deep))
      {
        RemoveFromSelection(removed->GetIdVal());
        RemoveBillboard(removed);
      }

      return removed;
    }

    void EditorScene::Destroy(bool removeResources)
    {
      ActionManager::GetInstance()->ClearAllActions();

      // If current scene getting destroyed, clear outliner.
      if (ScenePtr curr = GetSceneManager()->GetCurrentScene())
      {
        if (curr->IsSame(this))
        {
          if (OutlinerWindowPtr wnd = GetApp()->GetOutliner())
          {
            wnd->ClearOutliner();
          }
        }
      }

      Scene::Destroy(removeResources);

      m_selectedEntities.clear();

      // Destroy gizmos too.
      m_entityBillboardMap.clear();
      m_billboards.clear();
    }

    void EditorScene::GetSelectedEntities(EntityPtrArray& entities) const
    {
      for (ObjectId id : m_selectedEntities)
      {
        EntityPtr ntt = GetEntity(id);
        assert(ntt != nullptr && "Null entity found in the selection.");

        entities.push_back(ntt);
      }
    }

    void EditorScene::GetSelectedEntities(IDArray& entities) const { entities = m_selectedEntities; }

    void EditorScene::SelectByTag(const String& tag) { AddToSelection(GetByTag(tag), false); }

    Scene::PickData EditorScene::PickObject(const Ray& ray, const IDArray& ignoreList, const EntityPtrArray& extraList)
    {
      // Add billboards to scene
      EntityPtrArray temp = extraList;
      temp.insert(temp.end(), m_billboards.begin(), m_billboards.end());
      UpdateBillboardsForPicking();

      Scene::PickData pdata = Scene::PickObject(ray, ignoreList, temp);

      // If the billboards are picked, pick the entity
      if (pdata.entity != nullptr && pdata.entity->IsA<Billboard>() &&
          static_cast<Billboard*>(pdata.entity.get())->m_entity != nullptr)
      {
        pdata.entity = static_cast<Billboard*>(pdata.entity.get())->m_entity;
      }

      return pdata;
    }

    void EditorScene::PickObject(const Frustum& frustum,
                                 PickDataArray& pickedObjects,
                                 const IDArray& ignoreList,
                                 const EntityPtrArray& extraList)
    {
      EntityPtrArray extraWithBillboards = extraList;
      extraWithBillboards.insert(extraWithBillboards.end(), m_billboards.begin(), m_billboards.end());
      UpdateBillboardsForPicking();

      Scene::PickObject(frustum, pickedObjects, ignoreList, extraWithBillboards);

      // If the billboards are picked, pick the entity.
      pickedObjects.erase(std::remove_if(pickedObjects.begin(),
                                         pickedObjects.end(),
                                         [&pickedObjects](PickData& pd) -> bool
                                         {
                                           if (pd.entity == nullptr)
                                           {
                                             assert(false && "Pick should not create data with empty entity.");
                                             return true; // Remove null entity from pick data.
                                           }

                                           Entity* ntt = pd.entity.get();
                                           if (Billboard* billboard = ntt->As<Billboard>()) // If entity is a billboard.
                                           {
                                             if (billboard->m_entity == nullptr)
                                             {
                                               // Remove the billboard if it does not points an entity.
                                               return true;
                                             }

                                             // Check if billboard's entity is already picked.
                                             bool found = false;
                                             for (PickData& pd2 : pickedObjects)
                                             {
                                               if (pd2.entity->IsSame(billboard->m_entity))
                                               {
                                                 found = true;
                                                 break;
                                               }
                                             }

                                             if (found)
                                             {
                                               // Billboard's entity is already picked, remove the billboard.
                                               return true;
                                             }
                                             else
                                             {
                                               // Replace billboard with its entity.
                                               pd.entity = billboard->m_entity;
                                               return false;
                                             }
                                           }

                                           return false; // If entity is not a billboard don't remove.
                                         }),
                          pickedObjects.end());
    }

    void EditorScene::AddBillboard(EntityPtr entity)
    {
      auto addBillboardFn = [this, &entity](EditorBillboardPtr billboard)
      {
        if (m_entityBillboardMap.find(entity) != m_entityBillboardMap.end())
        {
          RemoveBillboard(entity);
        }

        // Note: Only 1 billboard per entity is supported.
        billboard->m_entity          = entity;
        m_entityBillboardMap[entity] = billboard;
        m_billboards.push_back(billboard);
      };

      // Check environment component
      bool envExist = entity->GetComponent<EnvironmentComponent>() != nullptr;
      if (envExist || entity->IsA<Sky>())
      {
        SkyBillboardPtr billboard = MakeNewPtr<SkyBillboard>();
        addBillboardFn(billboard);
        return;
      }

      // Check light
      if (entity->IsA<Light>())
      {
        LightBillboardPtr billboard = MakeNewPtr<LightBillboard>();
        addBillboardFn(billboard);
        return;
      }
    }

    void EditorScene::RemoveBillboard(EntityPtr entity)
    {
      if (m_entityBillboardMap.find(entity) != m_entityBillboardMap.end())
      {
        EntityPtr billboard = m_entityBillboardMap[entity];
        m_entityBillboardMap.erase(entity);
        for (auto it = m_billboards.begin(); it != m_billboards.end(); ++it)
        {
          if ((*it)->GetIdVal() == billboard->GetIdVal())
          {
            m_billboards.erase(it);
            break;
          }
        }
      }
    }

    EntityPtrArray EditorScene::GetBillboards() { return m_billboards; }

    EntityPtr EditorScene::GetBillboard(EntityPtr entity)
    {
      auto billBoard = m_entityBillboardMap.find(entity);
      if (billBoard != m_entityBillboardMap.end())
      {
        return billBoard->second;
      }
      else
      {
        return nullptr;
      }
    }

    void EditorScene::ValidateBillboard(EntityPtr entity)
    {
      bool addBillboard = false;
      auto billMap      = m_entityBillboardMap.find(entity);

      auto sanitizeFn   = [&addBillboard, billMap, this](EditorBillboardBase::BillboardType type) -> void
      {
        if (billMap != m_entityBillboardMap.end())
        {
          // Wrong type.
          if (billMap->second->GetBillboardType() != type)
          {
            // Override it with sky billboard.
            addBillboard = true;
          }
        }
        else
        {
          // Missing.
          addBillboard = true;
        }
      };

      // Check Sky billboard. Precedence over light billboard.
      if (entity->GetComponent<EnvironmentComponent>())
      {
        sanitizeFn(EditorBillboardBase::BillboardType::Sky);
      }
      else if (entity->IsA<Sky>())
      {
        sanitizeFn(EditorBillboardBase::BillboardType::Sky);
      }
      else if (entity->IsA<Light>())
      {
        sanitizeFn(EditorBillboardBase::BillboardType::Light);
      }
      else
      {
        if (billMap != m_entityBillboardMap.end())
        {
          // This entity should not have a billboard.
          RemoveBillboard(entity);
        }
      }

      if (addBillboard)
      {
        AddBillboard(entity);
      }
    }

    void EditorScene::ValidateBillboard(EntityPtrArray& entities)
    {
      for (EntityPtr ntt : entities)
      {
        ValidateBillboard(ntt);
      }
    }

    void EditorScene::CopyTo(Resource* other)
    {
      Super::CopyTo(other);
      EditorScene* cpy = static_cast<EditorScene*>(other);
      cpy->m_newScene  = true;
    }

    void EditorScene::UpdateBillboardsForPicking()
    {
      if (EditorViewportPtr vp = GetApp()->GetActiveViewport())
      {
        if (CameraPtr cam = vp->GetCamera())
        {
          for (EntityPtr billboard : m_billboards)
          {
            static_cast<Billboard*>(billboard.get())->LookAt(cam, vp->GetBillboardScale());
          }
        }
      }
    }

    void EditorScene::AddToSelectionSane(ObjectId id)
    {
      assert(id != NullHandle);
      assert(IsSelected(id) == false);
      m_selectedEntities.push_back(id);
    }

    // EditorSceneManager
    //////////////////////////////////////////

    EditorSceneManager::EditorSceneManager() {}

    EditorSceneManager::~EditorSceneManager() {}

  } // namespace Editor
} // namespace ToolKit
