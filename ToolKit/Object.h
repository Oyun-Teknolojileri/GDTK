/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Class.h"
#include "ParameterBlock.h"
#include "Serialize.h"
#include "Types.h"

namespace ToolKit
{

  // String Hash Utilities.
  //////////////////////////////////////////

  /**
   * 64 bit hash function for strings.
   * https://github.com/explosion/murmurhash/blob/master/murmurhash/MurmurHash2.cpp#L130
   */
  constexpr uint64_t MurmurHash64A(const void* key, int len, uint64_t seed)
  {
    const uint64_t m     = 0xc6a4a7935bd1e995;
    const int r          = 47;

    uint64_t h           = seed ^ (len * m);

    const uint64_t* data = (const uint64_t*) key;
    const uint64_t* end  = data + (len / 8);

    while (data != end)
    {
      uint64_t k  = *(data++);

      k          *= m;
      k          ^= k >> r;
      k          *= m;

      h          ^= k;
      h          *= m;
    }

    const unsigned char* data2 = (const unsigned char*) data;

    switch (len & 7)
    {
    case 7:
      h ^= uint64_t(data2[6]) << 48;
    case 6:
      h ^= uint64_t(data2[5]) << 40;
    case 5:
      h ^= uint64_t(data2[4]) << 32;
    case 4:
      h ^= uint64_t(data2[3]) << 24;
    case 3:
      h ^= uint64_t(data2[2]) << 16;
    case 2:
      h ^= uint64_t(data2[1]) << 8;
    case 1:
      h ^= uint64_t(data2[0]);
      h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
  }

  // Object
  //////////////////////////////////////////

  /**
   * Returns a static list of all registered classes.
   * This is used to register classes at startup, so they can be created dynamically later.
   */
  using RegisterFn = void (*)();

  inline std::vector<RegisterFn>& GetRegisterFnList()
  {
    static std::vector<RegisterFn> list;
    return list;
  }

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