/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Class.h"
#include "Hash.h"
#include "ParameterBlock.h"
#include "Serialize.h"
#include "Types.h"

namespace ToolKit
{

  // Object
  //////////////////////////////////////////

  /**
   * Returns the global list of all classes that registered themselves
   * via TKDefineClass. Object::Init walks this list to plug every
   * toolkit class into ObjectFactory.
   *
   * Implementation lives in Object.cpp, NOT inline in this header. An
   * inline static-local version compiles fine on its own, but the
   * `inline` keyword does not actually unify the static across
   * translation units that end up in different load modules (ToolKit.so
   * vs. Editor.exe). Each module then registers the same class twice
   * and ObjectFactory::Register trips its "already registered" assert.
   * One out-of-line definition, exported with TK_API, gives every
   * translation unit the same vector and the duplicate goes away.
   */
  using RegisterFn = void (*)();

  TK_API std::vector<RegisterFn>& GetRegisterFnList();

  /**
   * Base Macro that declares required fields and functions for each class that will be part of
   * ToolKit framework.
   */
#define TKDeclareClass(This, Base)                                                                                     \
 private:                                                                                                              \
  static ClassMeta This##Cls;                                                                                          \
  typedef Base Super;                                                                                                  \
                                                                                                                       \
 public:                                                                                                               \
  ClassMeta* const Class() const override;                                                                             \
  static ClassMeta* const StaticClass() { return &This##Cls; }                                                         \
  using Base::NativeConstruct;

#define TKDefineAbstractClass(This, Base)                                                                              \
  ClassMeta This::This##Cls = {Base::StaticClass(), #This, MurmurHash64A(#This, sizeof(#This), 41)};                   \
  ClassMeta* const This::Class() const { return &This##Cls; }

#define TKDefineClass(This, Base)                                                                                      \
  ClassMeta This::This##Cls = {Base::StaticClass(), #This, MurmurHash64A(#This, sizeof(#This), 41)};                   \
  ClassMeta* const This::Class() const { return &This##Cls; }                                                          \
  static struct _AutoRegister_##This                                                                                   \
  {                                                                                                                    \
    _AutoRegister_##This()                                                                                             \
    {                                                                                                                  \
      GetRegisterFnList().push_back([]() -> void { GetObjectFactory()->Register<This>(); });                           \
    }                                                                                                                  \
  } _autoRegister_##This;

  typedef std::shared_ptr<class Object> ObjectPtr;
  typedef std::weak_ptr<class Object> ObjectWeakPtr;

  /**
   * This base class provides basic reflection, type checking and serialization functionalities for ToolKit framework.
   */
  class TK_API Object : public Serializable
  {
   private:
    static ClassMeta ObjectCls;
    typedef Object Super;

   public:
    virtual ClassMeta* const Class() const;

    static ClassMeta* const StaticClass() { return &ObjectCls; }

    template <typename T, typename... Args>
    friend std::shared_ptr<T> MakeNewPtrCasted(const StringView Class, Args&&... args); //!< Friend constructor.

    template <typename T, typename... Args>
    friend std::shared_ptr<T> MakeNewPtr(Args&&... args); //!< Friend constructor.

   public:
    Object();
    virtual ~Object();
    virtual void NativeConstruct();
    virtual void NativeDestruct();
    virtual ObjectPtr Copy() const;

    template <typename T>
    bool IsA()
    {
      return Class()->IsSublcassOf(T::StaticClass());
    }

    template <typename T>
    T* As()
    {
      if (IsA<T>())
      {
        return static_cast<T*>(this);
      }

      return nullptr;
    }

    bool IsSame(const ObjectPtr& other) { return other->GetIdVal() == GetIdVal(); }

    bool IsSame(const Object* other) { return other->GetIdVal() == GetIdVal(); }

    template <typename T>
    std::shared_ptr<T> Self() const
    {
      return std::static_pointer_cast<T>(m_self.lock());
    }

   protected:
    /** Responsible for creating default components of the object. */
    virtual void ComponentConstructor();
    /** Responsible for creating default parameters of the object. */
    virtual void ParameterConstructor();
    /** Responsible for creating parameter events of the object. */
    virtual void ParameterEventConstructor();

    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

    void PreDeserializeImp(const SerializationFileInfo& info, XmlNode* parent) override;
    void PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

    /**
     * Utility function that checks if the current id is colliding with anything currently in the handle manager.
     * If a collision happens, it sets _idBeforeCollision with the colliding id to resolve parent - child relations
     * and assigns a new non colliding id.
     */
    void PreventIdCollision();

   public:
    TKDeclareParam(ObjectId, Id);

    /**
     * Storage for all ParameterVariants declared for this object and its derivatives.
     */
    ParameterBlock m_localData;

    /**
     * This is internally used to match parent, child pairs.
     * If a collision occurs, the original value is stored here to be used in parent - child matching.
     */
    ObjectId _idBeforeCollision;

   private:
    ObjectWeakPtr m_self;
  };

} // namespace ToolKit