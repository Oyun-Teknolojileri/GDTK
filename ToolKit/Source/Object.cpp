/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Object.h"

#include "Entity.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{
  std::vector<RegisterFn>& GetRegisterFnList()
  {
    static std::vector<RegisterFn> list;
    return list;
  }

  TKDefineClass(Object, Object);

  Object::Object()
  {
    _idBeforeCollision = NullHandle;

    // The live object count lives in the constructor and the destructor, which run exactly once
    // per object. ParameterConstructor() is not a safe place for the count: derived classes may
    // call it again, which would count the same object twice.
    if (HandleManager* handleMan = GetHandleManager())
    {
      handleMan->ObjectCreated();
    }
  }

  Object::~Object()
  {
    if (HandleManager* handleMan = GetHandleManager())
    {
      handleMan->ReleaseHandle(GetIdVal());
      handleMan->ObjectDestroyed();
      TK_UNTRACK_LIVE_OBJECT(handleMan, this);
    }
  }

  void Object::NativeConstruct()
  {
    ComponentConstructor();
    ParameterConstructor();
    ParameterEventConstructor();
  }

  void Object::NativeDestruct() {}

  void Object::ComponentConstructor() {}

  void Object::ParameterConstructor()
  {
    HandleManager* handleMan = GetHandleManager();
    ObjectId id              = handleMan->GenerateHandle();
    Id_Define(id, EntityCategory.Name, EntityCategory.Priority, true, false);

    // Debug only class registry, so Main::PostUninit can name what was left behind. This has to
    // run here, not in Object::Object(), because Class() still reports the base type while the
    // base constructor runs. Recording is idempotent (keyed by address), so a derived class
    // calling this a second time does not register the same object twice.
    TK_TRACK_LIVE_OBJECT(handleMan, this);
  }

  void Object::ParameterEventConstructor() {}

  ObjectPtr Object::Copy() const { return nullptr; }

  XmlNode* Object::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    assert(doc != nullptr && parent != nullptr);

    XmlNode* objNode = CreateXmlNode(doc, StaticClass()->Name, parent);
    WriteAttr(objNode, doc, XmlObjectClassAttr, Class()->Name);

    m_localData.Serialize(doc, objNode);
    return objNode;
  }

  XmlNode* Object::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    assert(parent != nullptr && "Root of the object can't be null.");

    ObjectId id = GetIdVal();
    GetHandleManager()->ReleaseHandle(id);
    m_localData.m_version = m_version;
    m_localData.DeSerialize(info, parent);
    PreventIdCollision();

    // Construction progress from bottom up.
    return parent;
  }

  void Object::PreDeserializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    Serializable::PreDeserializeImp(info, parent);

    // Clear parameters created on native constructor and reconstruct them after deserialized.
    for (ParameterVariant& param : m_localData.m_variants)
    {
      param.m_onValueChangedFn.clear();
    }
  }

  void Object::PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    Serializable::PostDeSerializeImp(info, parent);
    ParameterEventConstructor(); // Set all the events after data deserialized.
  }

  void Object::PreventIdCollision()
  {
    HandleManager* handleMan = GetHandleManager();
    ObjectId idInFile        = GetIdVal();

    if (!handleMan->IsHandleUnique(idInFile))
    {
      _idBeforeCollision = idInFile;
      SetIdVal(handleMan->GenerateHandle());
    }
    else
    {
      handleMan->AddHandle(idInFile);
    }
  }

} // namespace ToolKit
