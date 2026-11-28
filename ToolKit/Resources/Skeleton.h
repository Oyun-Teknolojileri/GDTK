/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Resource.h"
#include "ResourceManager.h"

#include <map>

namespace ToolKit
{

  /** Represent a bone's initial location in a skeleton.  */
  class TK_API StaticBone
  {
   public:
    StaticBone(const String& name);
    ~StaticBone();

   public:
    String m_name;
    Mat4 m_inverseWorldMatrix;
  };

  /* This class is used to store dynamic bone information. */
  class TK_API DynamicBoneMap
  {
   public:
    struct DynamicBone
    {
      // Should be same with matching static bone's element index
      uint boneIndx = 0;
      Node* node    = nullptr;

      // Local bind transform of the bone, captured while the map is built. The animation data
      // texture uses it for bones a clip does not animate; deriving it from the inverse bind
      // matrix is not possible because that matrix does not carry the mesh-to-skeleton
      // conversion.
      Vec3 m_restTranslation    = Vec3(0.0f);
      Quaternion m_restRotation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
      Vec3 m_restScale          = Vec3(1.0f);
    };

    DynamicBoneMap();
    ~DynamicBoneMap();

    void Init(const Skeleton* skeleton);

    void ForEachRootBone(std::function<void(const DynamicBone*)> childProcessFunc) const;
    void AddDynamicBone(const String& boneName, DynamicBone& bone, DynamicBone* parent);

   public:
    std::map<String, DynamicBone> m_boneMap;
  };

  class TK_API Skeleton : public Resource
  {
   public:
    TKDeclareClass(Skeleton, Resource);

    Skeleton();
    explicit Skeleton(const String& file);
    virtual ~Skeleton();

    void Init(bool flushClientSideArray = false) override;
    void UnInit() override;
    void Load() override;

    int GetBoneIndex(const String& bone);
    StaticBone* GetBone(const String& bone);

   protected:
    void CopyTo(Resource* other) override;
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

   public:
    std::vector<StaticBone*> m_bones;
    DynamicBoneMap m_Tpose;
    TexturePtr m_bindPoseTexture = nullptr;
  };

  class TK_API SkeletonManager : public ResourceManager
  {
   public:
    SkeletonManager();
    virtual ~SkeletonManager();
    bool CanStore(ClassMeta* Class) override;
  };

} // namespace ToolKit
