/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Entity.h"

#include "AABBOverrideComponent.h"
#include "Audio.h"
#include "Camera.h"
#include "Canvas.h"
#include "DirectionComponent.h"
#include "Drawable.h"
#include "GradientSky.h"
#include "Light.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "Node.h"
#include "Prefab.h"
#include "Primative.h"
#include "Scene.h"
#include "Skeleton.h"
#include "Sky.h"
#include "Surface.h"
#include "ToolKit.h"
#include "Util.h"

#include <DebugNew.h>

namespace ToolKit
{

  TKDefineClass(Entity, Object);

  Entity::Entity()
  {
    m_node            = new Node();
    _prefabRootEntity = nullptr;
    _parentId         = NullHandle;
  }

  Entity::~Entity()
  {
    SafeDel(m_node);
    ClearComponents();
  }

  void Entity::NativeConstruct()
  {
    Super::NativeConstruct();
    m_node->OwnerEntity(Self<Entity>());
  }

  EntityPtr Entity::Parent() const
  {
    if (m_node)
    {
      if (m_node->m_parent)
      {
        if (EntityPtr parent = m_node->m_parent->OwnerEntity())
        {
          return parent;
        }
      }
    }
    return nullptr;
  }

  bool Entity::IsDrawable() const
  {
    if (MeshComponent* meshComp = GetComponentFast<MeshComponent>())
    {
      if (const MeshPtr& mesh = meshComp->GetMeshVal())
      {
        return mesh->TotalVertexCount() > 0;
      }
    }

    return false;
  }

  void Entity::SetPose(const AnimationPtr& anim, float time)
  {
    MeshComponentPtr meshComp = GetMeshComponent();
    if (meshComp)
    {
      MeshPtr mesh                  = meshComp->GetMeshVal();
      SkeletonComponentPtr skelComp = GetComponent<SkeletonComponent>();
      if (mesh->IsSkinned() && skelComp)
      {
        anim->GetPose(skelComp, time);
        return;
      }
    }
    anim->GetPose(m_node, time);
  }

  const BoundingBox& Entity::GetBoundingBox(bool inWorld)
  {
    if (!m_spatialCachesInvalidated)
    {
      return inWorld ? m_worldBoundingBoxCache : m_localBoundingBoxCache;
    }

    UpdateSpatialCaches();

    return inWorld ? m_worldBoundingBoxCache : m_localBoundingBoxCache;
  }

  ObjectPtr Entity::Copy() const
  {
    EntityPtr cpy = MakeNewPtrCasted<Entity>(Class()->Name);
    CopyTo(cpy.get());

    return cpy;
  }

  void Entity::ClearComponents() { m_components.clear(); }

  Entity* Entity::GetPrefabRoot() const { return _prefabRootEntity; }

  void Entity::InvalidateSpatialCaches()
  {
    if (DirectionComponent* dirComp = GetComponentFast<DirectionComponent>())
    {
      dirComp->m_spatialCachesInvalidated = true;
    }

    if (EnvironmentComponent* envComp = GetComponentFast<EnvironmentComponent>())
    {
      envComp->m_spatialCachesInvalidated = true;
    }

    if (m_aabbTreeNodeProxy != AABBTree::nullNode)
    {
      if (ScenePtr scene = m_scene.lock())
      {
        scene->m_aabbTree.Invalidate(m_aabbTreeNodeProxy);
      }
    }

    m_spatialCachesInvalidated = true;
  }

  Entity* Entity::CopyTo(Entity* other) const
  {
    WeakCopy(other, false);
    other->ClearComponents();
    for (int i = 0; i < (int) m_components.size(); i++)
    {
      ComponentPtr copy = m_components[i]->Copy(other->Self<Entity>());
      other->m_components.push_back(copy);
    }

    return other;
  }

  void Entity::ParameterConstructor()
  {
    Super::ParameterConstructor();

    Name_Define(Class()->Name, EntityCategory.Name, EntityCategory.Priority, true, true);
    Tag_Define("", EntityCategory.Name, EntityCategory.Priority, true, true);
    Visible_Define(true, EntityCategory.Name, EntityCategory.Priority, true, true);
    TransformLock_Define(false, EntityCategory.Name, EntityCategory.Priority, true, true);
  }

  void Entity::ParameterEventConstructor() { Super::ParameterEventConstructor(); }

  void Entity::WeakCopy(Entity* other, bool copyComponents) const
  {
    assert(other->Class() == Class());
    SafeDel(other->m_node);
    other->m_node = m_node->Copy();
    other->m_node->OwnerEntity(other->Self<Entity>());

    // Preserve Ids.
    ObjectId id        = other->GetIdVal();
    other->m_localData = m_localData;
    other->SetIdVal(id);

    if (copyComponents)
    {
      other->m_components = m_components;
    }
  }

  void Entity::AddComponent(const ComponentPtr& component)
  {
    assert(GetComponent(component->Class()) == nullptr && "Component has already been added.");
    component->OwnerEntity(Self<Entity>());
    m_components.push_back(component);
  }

  MeshComponentPtr Entity::GetMeshComponent() const { return GetComponent<MeshComponent>(); }

  MaterialComponentPtr Entity::GetMaterialComponent() const { return GetComponent<MaterialComponent>(); }

  ComponentPtr Entity::RemoveComponent(ClassMeta* Class)
  {
    for (int i = 0; i < (int) m_components.size(); i++)
    {
      if (m_components[i]->Class() == Class)
      {
        ComponentPtr cmp = m_components[i];
        m_components.erase(m_components.begin() + i);
        return cmp;
      }
    }

    return nullptr;
  }

  ComponentPtrArray& Entity::GetComponentPtrArray() { return m_components; }

  const ComponentPtrArray& Entity::GetComponentPtrArray() const { return m_components; }

  ComponentPtr Entity::GetComponent(ClassMeta* Class) const
  {
    for (int i = 0; i < (int) m_components.size(); i++)
    {
      if (m_components[i]->Class() == Class)
      {
        return m_components[i];
      }
    }

    return nullptr;
  }

  void Entity::DeserializeComponents(const SerializationFileInfo& info, XmlNode* entityNode)
  {
    ClearComponents();

    if (XmlNode* comArray = entityNode->first_node(XmlComponentArrayElement.data()))
    {
      XmlNode* comNode = comArray->first_node(Object::StaticClass()->Name.c_str());
      while (comNode != nullptr)
      {
        String cls;
        ReadAttr(comNode, XmlObjectClassAttr.data(), cls);
        ComponentPtr com = MakeNewPtrCasted<Component>(cls);
        com->m_version   = m_version;
        com->DeSerialize(info, comNode);

        AddComponent(com);

        comNode = comNode->next_sibling();
      }
    }
  }

  XmlNode* Entity::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* objNode = Super::SerializeImp(doc, parent);
    XmlNode* node    = CreateXmlNode(doc, StaticClass()->Name, objNode);

    if (EntityPtr parentNtt = m_node->ParentEntity())
    {
      WriteAttr(node, doc, XmlParentEntityIdAttr, std::to_string(parentNtt->GetIdVal()));
    }

    m_node->Serialize(doc, node);

    XmlNode* compNode = CreateXmlNode(doc, XmlComponentArrayElement, node);
    for (auto& cmp : GetComponentPtrArray())
    {
      cmp->Serialize(doc, compNode);
    }

    return node;
  }

  XmlNode* Entity::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    if (m_version >= TKV045)
    {
      return DeSerializeImpV045(info, parent);
    }

    // Old file, keep parsing.
    XmlNode* nttNode = nullptr;
    XmlDocument* doc = info.Document;

    if (parent != nullptr)
    {
      nttNode = parent;
    }
    else
    {
      nttNode = doc->first_node(XmlEntityElement.c_str());
    }

    XmlNode* node = nttNode;
    ReadAttr(node, XmlParentEntityIdAttr, _parentId);

    if (XmlNode* transformNode = node->first_node(XmlNodeElement.c_str()))
    {
      m_node->DeSerialize(info, transformNode);
    }

    // Release the generated id.
    HandleManager* handleMan = GetHandleManager();
    ObjectId id              = GetIdVal();
    handleMan->ReleaseHandle(id);

    // Read id and other parameters.
    m_localData.DeSerialize(info, parent);

    PreventIdCollision();

    ClearComponents();

    if (XmlNode* components = node->first_node("Components"))
    {
      XmlNode* comNode = components->first_node(XmlComponent.c_str());
      while (comNode)
      {
        int type = -1;
        ReadAttr(comNode, XmlParamterTypeAttr, type);
        ComponentPtr com = ComponentFactory::Create((ComponentFactory::ComponentType) type);
        com->m_version   = m_version;
        com->DeSerialize(info, comNode);
        AddComponent(com);

        comNode = comNode->next_sibling();
      }
    }

    return nttNode;
  }

  XmlNode* Entity::DeSerializeImpV045(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlNode* objNode = Super::DeSerializeImp(info, parent);
    XmlNode* nttNode = objNode->first_node(Entity::StaticClass()->Name.c_str());
    ReadAttr(nttNode, XmlParentEntityIdAttr, _parentId);

    if (XmlNode* transformNode = nttNode->first_node(XmlNodeElement.c_str()))
    {
      m_node->DeSerialize(info, transformNode);
    }

    DeserializeComponents(info, nttNode);

    return nttNode;
  }

  void Entity::UpdateLocalBoundingBox()
  {
    if (MeshComponent* meshComp = GetComponentFast<MeshComponent>())
    {
      m_localBoundingBoxCache = meshComp->GetBoundingBox();
    }
    else
    {
      m_localBoundingBoxCache = infinitesimalBox;
    }
  }

  void Entity::UpdateSpatialCaches()
  {
    // Update bounding box.
    UpdateLocalBoundingBox();

    if (AABBOverrideComponent* overrideComp = GetComponentFast<AABBOverrideComponent>())
    {
      m_localBoundingBoxCache = overrideComp->GetBoundingBox();
    }

    if (!m_localBoundingBoxCache.IsValid())
    {
      // In case of an uninitialized bounding box, provide a very small box.
      m_localBoundingBoxCache = infinitesimalBox;
    }

    m_worldBoundingBoxCache = m_localBoundingBoxCache;
    TransformAABB(m_worldBoundingBoxCache, m_node->GetTransform());

    m_spatialCachesInvalidated = false;
  }

  void Entity::RemoveResources() { assert(false && "Not implemented"); }

  bool Entity::IsVisible()
  {
    if (Entity* root = GetPrefabRoot())
    {
      // If parent is not visible, all objects must be hidden.
      // Otherwise, prefer to use its value.
      if (root->GetVisibleVal() == false)
      {
        return false;
      }
    }

    return GetVisibleVal();
  }

  void Entity::SetVisibility(bool vis, bool deep)
  {
    SetVisibleVal(vis);
    if (deep)
    {
      EntityPtrArray children;
      GetChildren(Self<Entity>(), children);
      for (EntityPtr child : children)
      {
        child->SetVisibility(vis, true);
      }
    }
  }

  void Entity::SetTransformLock(bool lock, bool deep)
  {
    SetTransformLockVal(lock);
    if (deep)
    {
      EntityPtrArray children;
      GetChildren(Self<Entity>(), children);
      for (EntityPtr child : children)
      {
        child->SetTransformLock(lock, true);
      }
    }
  }

  void TraverseEntityHierarchyBottomUp(EntityPtr parent, const std::function<void(EntityPtr child)>& callbackFn)
  {
    return TraverseNodeHierarchyBottomUp(parent->m_node, [&](Node* node) -> void { callbackFn(node->OwnerEntity()); });
  }

  EntityNode::EntityNode() { m_partOfAABBTree = false; }

  EntityNode::EntityNode(const String& name) { SetNameVal(name); }

  EntityNode::~EntityNode() {}

  void EntityNode::RemoveResources() {}

  XmlNode* EntityNode::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

  EntityPtr EntityFactory::CreateByType(EntityType type)
  {
    EntityPtr ntt = nullptr;
    switch (type)
    {
    case EntityType::Entity_Base:
      ntt = MakeNewPtr<Entity>();
      break;
    case EntityType::Entity_Node:
      ntt = MakeNewPtr<EntityNode>();
      break;
    case EntityType::Entity_AudioSource:
      ntt = MakeNewPtr<AudioSource>();
      break;
    case EntityType::Entity_Billboard:
      ntt = MakeNewPtr<Billboard>();
      break;
    case EntityType::Entity_Cube:
      ntt = MakeNewPtr<Cube>();
      break;
    case EntityType::Entity_Quad:
      ntt = MakeNewPtr<Quad>();
      break;
    case EntityType::Entity_Sphere:
      ntt = MakeNewPtr<Sphere>();
      break;
    case EntityType::Entity_Arrow:
      ntt = MakeNewPtr<Arrow2d>();
      break;
    case EntityType::Entity_LineBatch:
      ntt = MakeNewPtr<LineBatch>();
      break;
    case EntityType::Entity_Cone:
      ntt = MakeNewPtr<Cone>();
      break;
    case EntityType::Entity_Drawable:
      ntt = MakeNewPtr<Drawable>();
      break;
    case EntityType::Entity_Camera:
      ntt = MakeNewPtr<Camera>();
      break;
    case EntityType::Entity_Surface:
      ntt = MakeNewPtr<Surface>();
      break;
    case EntityType::Entity_Button:
      ntt = MakeNewPtr<Button>();
      break;
    case EntityType::Entity_Light:
      ntt = MakeNewPtr<Light>();
      break;
    case EntityType::Entity_DirectionalLight:
      ntt = MakeNewPtr<DirectionalLight>();
      break;
    case EntityType::Entity_PointLight:
      ntt = MakeNewPtr<PointLight>();
      break;
    case EntityType::Entity_SpotLight:
      ntt = MakeNewPtr<SpotLight>();
      break;
    case EntityType::Entity_Sky:
      ntt = MakeNewPtr<Sky>();
      break;
    case EntityType::Entity_GradientSky:
      ntt = MakeNewPtr<GradientSky>();
      break;
    case EntityType::Entity_Canvas:
      ntt = MakeNewPtr<Canvas>();
      break;
    case EntityType::Entity_Prefab:
      ntt = MakeNewPtr<Prefab>();
      break;
    case EntityType::Entity_SpriteAnim:
    case EntityType::UNUSEDSLOT_1:
    default:
      assert(false);
      break;
    }

    return ntt;
  }

  TKDefineClass(EntityNode, Entity);

} // namespace ToolKit
