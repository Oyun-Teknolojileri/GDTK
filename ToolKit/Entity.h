/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

/**
 * @file Entity.h Header for Entity,
 */

#include "AABBTree.h"
#include "Animation.h"
#include "MaterialComponent.h"
#include "MeshComponent.h"
#include "Node.h"
#include "Object.h"
#include "ToolKit.h"
#include "Types.h"

namespace ToolKit
{

  // Entity
  //////////////////////////////////////////

  static VariantCategory EntityCategory {"Meta", 100};

  /**
   * Fundamental object that all the ToolKit utilities can interacted with.
   * Entity is the base class for all the objects that can be inserted in any
   * scene.
   */
  class TK_API Entity : public Object
  {
   public:
    TKDeclareClass(Entity, Object);

    Entity();
    virtual ~Entity();

    void NativeConstruct() override;

    EntityPtr Parent() const;
    virtual bool IsDrawable() const;
    virtual void SetPose(const AnimationPtr& anim, float time);
    virtual const BoundingBox& GetBoundingBox(bool inWorld = false);
    ObjectPtr Copy() const override;
    virtual void RemoveResources();

    /**
     * Returns the visibility status of the current Entity. If it belongs to a
     * prefab, it returns the visibility of Prefab.
     */
    virtual bool IsVisible();

    void SetVisibility(bool vis, bool deep);
    void SetTransformLock(bool vis, bool deep);

    template <typename T>
    std::shared_ptr<T> AddComponent(bool componentSerializable = true)
    {
      assert(GetComponent(T::StaticClass()) == nullptr && "Component has already been added.");

      std::shared_ptr<T> component = MakeNewPtr<T>(componentSerializable);
      component->OwnerEntity(Self<Entity>());
      m_components.push_back(component);
      return component;
    }

    void AddComponent(const ComponentPtr& component);

    /**
     * Used to easily access first MeshComponentPtr.
     * @return First MeshComponentPtr if any otherwise empty pointer.
     */
    MeshComponentPtr GetMeshComponent() const;

    /**
     * Used to easily access first MaterialComponentPtr.
     * @return First MaterialComponentPtr if any exist, otherwise empty pointer.
     */
    MaterialComponentPtr GetMaterialComponent() const;

    /**
     * Remove the given component from the components of the Entity.
     * @param componentId Id of the component to be removed.
     * @return Removed ComponentPtr. If nothing gets removed, returns nullptr.
     */
    template <typename T>
    ComponentPtr RemoveComponent()
    {
      for (int i = 0; i < (int) m_components.size(); i++)
      {
        if (m_components[i]->IsA<T>())
        {
          ComponentPtr cmp = m_components[i];
          m_components.erase(m_components.begin() + i);
          return cmp;
        }
      }

      return nullptr;
    }

    /**
     * Remove the given component from the components of the Entity.
     * @param Class is the class of the component to be removed.
     * @return Removed ComponentPtr. If nothing gets removed, returns nullptr.
     */
    ComponentPtr RemoveComponent(ClassMeta* Class);

    /**
     * Mutable component array accessors.
     * @return ComponentPtrArray for this Entity.
     */
    ComponentPtrArray& GetComponentPtrArray();

    /**
     * Immutable component array accessors.
     * @return ComponentPtrArray for this Entity.
     */
    const ComponentPtrArray& GetComponentPtrArray() const;

    /**
     * Used to return component of type T.
     * @return Component of type T if exist, otherwise nullptr.
     */
    template <typename T>
    std::shared_ptr<T> GetComponent() const
    {
      for (int i = 0; i < (int) m_components.size(); i++)
      {
        if (m_components[i]->IsA<T>())
        {
          return Cast<T>(m_components[i]);
        }
      }

      return nullptr;
    }

    /** Faster version of the get component, if raw pointer is applicable. */
    template <typename T>
    T* GetComponentFast() const
    {
      for (int i = 0; i < (int) m_components.size(); i++)
      {
        if (m_components[i]->IsA<T>())
        {
          return static_cast<T*>(m_components[i].get());
        }
      }

      return nullptr;
    }

    /**
     * Returns the component with the given class.
     * @return Component of type T if exist, otherwise empty pointer.
     */
    ComponentPtr GetComponent(ClassMeta* Class) const;

    /** Removes all components from the entity. */
    void ClearComponents();

    /**
     * Used to identify if this Entity is a prefab, and if so, returns the
     * pointer to the parent prefab.
     * @return If the entity belongs to a Prefab it returns the pointer of the
     * prefab, otherwise it returns nullptr.
     */
    Entity* GetPrefabRoot() const;

    /** Bounding boxes, AABB tree are invalidated. */
    virtual void InvalidateSpatialCaches();

    /** Updates spatial caches related to entity. AABB tree is updated upon access. */
    virtual void UpdateSpatialCaches();

   protected:
    virtual Entity* CopyTo(Entity* other) const;
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;
    void WeakCopy(Entity* other, bool copyComponents = true) const;

    /** Default component deserializer, clears all default components and use serialized ones. */
    virtual void DeserializeComponents(const SerializationFileInfo& info, XmlNode* entityNode);

    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;
    XmlNode* DeSerializeImpV045(const SerializationFileInfo& info, XmlNode* parent);

    virtual void UpdateLocalBoundingBox();

   public:
    TKDeclareParam(String, Name);
    TKDeclareParam(String, Tag);
    TKDeclareParam(bool, Visible);
    TKDeclareParam(bool, TransformLock);

    /** Node that holds the transform and parenting data for the Entity. */
    Node* m_node;

    /**
     * Internally used variable.
     * Helper ID for entity De serialization. Points to parent of the entity.
     */
    ObjectId _parentId;

    /**
     * Internally used variable.
     * Used to indicate this entity belongs to a prefab entity. Set by the
     * Prefab Entity during Prefab::Init.
     */
    Entity* _prefabRootEntity;

    /** Index into the bvh tree that points to the node for this entity. */
    AABBNodeProxy m_aabbTreeNodeProxy = AABBTree::nullNode;

    /** Entity causes AABBTree to be updated when added removed to the scene. */
    bool m_partOfAABBTree             = true;

    /** The Scene that entity belongs to. */
    SceneWeakPtr m_scene;

    /** If true, transform related caches (aabb, abbtree etc...) are updated upon access. */
    bool m_spatialCachesInvalidated = true;

   protected:
    BoundingBox m_localBoundingBoxCache;
    BoundingBox m_worldBoundingBoxCache;

   private:
    /**
     * Component map that may contains only one component per type.
     * It holds Class HashId - ComponentPtr
     */
    ComponentPtrArray m_components;
  };

  // Entity Container functions.
  //////////////////////////////////////////

  /** Move entities of type T to filtered array. */
  template <typename T>
  void MoveByType(EntityRawPtrArray& entities, std::vector<T*>& filtered)
  {
    auto it = std::partition(entities.begin(),
                             entities.end(),
                             [&filtered](Entity* ntt)
                             {
                               if (static_cast<Entity*>(ntt)->IsA<T>())
                               {
                                 filtered.emplace_back(static_cast<T*>(ntt));
                                 return false; // Keep elements of type T.
                               }
                               return true; // Remove elements thats not of type T.
                             });

    entities.erase(it, entities.end());
  }

  /**
   * Recursively traverse each child of the parent and apply callback function.
   * Execution order is bottom - up. Meaning that lambda called on leafs than parent.
   */
  TK_API void TraverseEntityHierarchyBottomUp(EntityPtr parent, const std::function<void(EntityPtr child)>& callbackFn);

  // EntityNode
  //////////////////////////////////////////

  class TK_API EntityNode : public Entity
  {
   public:
    TKDeclareClass(EntityNode, Entity);

    EntityNode();
    EntityNode(const String& name);
    virtual ~EntityNode();

    void RemoveResources() override;

   protected:
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
  };

  // EntityFactory
  //////////////////////////////////////////

  /**
   * DEPRECATED use ObjectFactory
   * Utility class to construct Entity.
   */
  class EntityFactory final
  {
   public:
    /**
     * Enums that shows the type of the Entity. Each derived class should provide
     * a type identifier for itself to make itself known to the ToolKit.
     */
    enum class EntityType
    {
      // Order is important. Don't change for backward comparable scene files.
      Entity_Base,
      Entity_AudioSource,
      Entity_Billboard,
      Entity_Cube,
      Entity_Quad,
      Entity_Sphere,
      Entity_Arrow,
      Entity_LineBatch,
      Entity_Cone,
      Entity_Drawable,
      Entity_SpriteAnim,
      Entity_Surface,
      Entity_Light,
      Entity_Camera,
      UNUSEDSLOT_1,
      Entity_Node,
      Entity_Button,
      Entity_Sky,
      Entity_DirectionalLight,
      Entity_PointLight,
      Entity_SpotLight,
      Entity_Canvas,
      Entity_Prefab,
      Entity_SkyBase,
      Entity_GradientSky,
      ENTITY_TYPE_COUNT // Holds the size of the enum
    };

   public:
    static EntityPtr CreateByType(EntityType type);
  };

} // namespace ToolKit
