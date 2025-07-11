/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "ObjectFactory.h"

#include "AABBOverrideComponent.h"
#include "AnimationControllerComponent.h"
#include "Audio.h"
#include "Camera.h"
#include "Canvas.h"
#include "DirectionComponent.h"
#include "Dpad.h"
#include "Drawable.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "Framebuffer.h"
#include "GradientSky.h"
#include "Light.h"
#include "Material.h"
#include "MaterialComponent.h"
#include "Mesh.h"
#include "MeshComponent.h"
#include "Prefab.h"
#include "Primative.h"
#include "Scene.h"
#include "Shader.h"
#include "SkeletonComponent.h"
#include "Sky.h"
#include "SpriteSheet.h"
#include "SsaoPass.h"
#include "Surface.h"
#include "Texture.h"

#include "DebugNew.h"

namespace ToolKit
{

  ObjectFactory::ObjectFactory() {}

  ObjectFactory::~ObjectFactory() {}

  void ObjectFactory::CallMetaProcessors(const MetaMap& metaKeys, const MetaProcessorMap& metaProcessorMap)
  {
    for (auto& meta : metaKeys)
    {
      auto metaProcessor = metaProcessorMap.find(meta.first);
      if (metaProcessor != metaProcessorMap.end())
      {
        if (metaProcessor->second != nullptr)
        {
          metaProcessor->second(meta.second);
        }
      }
    }
  }

  void ObjectFactory::ClassLookUpBuilder(ClassMeta* Class, ClassMeta* FirstClass)
  {
    if (Class->Super)
    {
      if (Class->Super == Object::StaticClass())
      {
        FirstClass->SuperClassLookUp.push_back({Class->Name, Class->HashId});

        ClassMeta* objCls = Object::StaticClass();
        FirstClass->SuperClassLookUp.push_back({objCls->Name, objCls->HashId});
      }
      else
      {
        FirstClass->SuperClassLookUp.push_back({Class->Name, Class->HashId});
        ClassLookUpBuilder(Class->Super, FirstClass);
      }
    }
  }

  ObjectFactory::ObjectConstructorCallback& ObjectFactory::GetConstructorFn(const StringView Class)
  {
    auto constructorFnIt = m_constructorFnMap.find(Class);
    if (constructorFnIt != m_constructorFnMap.end())
    {
      return constructorFnIt->second;
    }

    return m_nullFn;
  }

  Object* ObjectFactory::MakeNew(const StringView Class)
  {
    if (auto constructorFn = GetConstructorFn(Class))
    {
      Object* object = constructorFn();
      return object;
    }

    assert(false && "Unknown object type.");
    return nullptr;
  }

  void ObjectFactory::Init()
  {
    for (auto fn : GetRegisterFnList())
    {
      fn();
    }
  }

} // namespace ToolKit
