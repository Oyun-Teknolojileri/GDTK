/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Animation.h"

#include "AnimationControllerComponent.h"
#include "Entity.h"
#include "FileManager.h"
#include "Material.h"
#include "MaterialComponent.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "Node.h"
#include "Skeleton.h"
#include "ToolKit.h"
#include "Util.h"
#include "utilities/base64.h"

#include "DebugNew.h"

#include <algorithm>

static constexpr bool SERIALIZE_ANIMATION_AS_BINARY = true;

namespace ToolKit
{
  namespace
  {
    /** Finds a parameter of a block by name, nullptr when the block has no such parameter. */
    ParameterVariant* FindParam(ParameterBlock& block, const String& name)
    {
      for (ParameterVariant& var : block.m_variants)
      {
        if (var.m_name == name)
        {
          return &var;
        }
      }

      return nullptr;
    }

    /**
     * Nearest parameter keys around a time. Mirrors GetNearestKeys(), which works on transform keys.
     * The track has to stay ascending by frame for this to hold, which SetParamKey() guarantees.
     */
    void FindParamKeys(const ParamKeyArray& keys, float fps, float t, int& key1, int& key2, float& ratio)
    {
      key1  = -1;
      key2  = -1;
      ratio = 0.0f;

      const int count = static_cast<int>(keys.size());
      if (count == 0)
      {
        return;
      }

      if (count == 1)
      {
        key1 = 0;
        key2 = 0;
        return;
      }

      // Earlier than the first key or later than the last one: hold the boundary key.
      if (keys.front().m_frame / fps > t)
      {
        key1 = 0;
        key2 = 1;
        return;
      }

      if (t > keys.back().m_frame / fps)
      {
        key1  = count - 2;
        key2  = count - 1;
        ratio = 1.0f;
        return;
      }

      for (int i = 1; i < count; i++)
      {
        const float keyTime2 = keys[i].m_frame / fps;
        const float keyTime1 = keys[i - 1].m_frame / fps;

        if (t >= keyTime1 && keyTime2 >= t)
        {
          ratio = (t - keyTime1) / (keyTime2 - keyTime1);
          key1  = i - 1;
          key2  = i;
          return;
        }
      }
    }

    /** States if a parameter type blends between keys instead of holding the previous one. */
    bool IsInterpolatedParam(ParameterVariant::VariantType type)
    {
      switch (type)
      {
        case ParameterVariant::VariantType::Float:
        case ParameterVariant::VariantType::Vec2:
        case ParameterVariant::VariantType::Vec3:
        case ParameterVariant::VariantType::Vec4:
          return true;
        default:
          return false;
      }
    }
  } // namespace

  TKDefineClass(Animation, Resource);

  Animation::Animation() {}

  Animation::Animation(const String& file) : Animation() { SetFile(file); }

  Animation::~Animation() { UnInit(); }

  // Parameter tracks
  //////////////////////////////////////////

  bool Animation::IsParamTypeAnimatable(ParameterVariant::VariantType type)
  {
    switch (type)
    {
      case ParameterVariant::VariantType::Float:
      case ParameterVariant::VariantType::Int:
      case ParameterVariant::VariantType::UInt:
      case ParameterVariant::VariantType::Byte:
      case ParameterVariant::VariantType::Ubyte:
      case ParameterVariant::VariantType::Bool:
      case ParameterVariant::VariantType::Vec2:
      case ParameterVariant::VariantType::Vec3:
      case ParameterVariant::VariantType::Vec4:
        return true;
      default:
        // Resource pointers, callbacks, record maps, matrices and multi choice parameters are not
        // keyframable: a choice is a discrete state, not a curve.
        return false;
    }
  }

  bool Animation::PackParamValue(const ParameterVariant& var, Vec4& value)
  {
    value = Vec4(0.0f);

    switch (var.GetType())
    {
      case ParameterVariant::VariantType::Float:
        value.x = var.GetCVar<float>();
        break;
      case ParameterVariant::VariantType::Int:
        value.x = (float) var.GetCVar<int>();
        break;
      case ParameterVariant::VariantType::UInt:
        value.x = (float) var.GetCVar<uint>();
        break;
      case ParameterVariant::VariantType::Byte:
        value.x = (float) var.GetCVar<byte>();
        break;
      case ParameterVariant::VariantType::Ubyte:
        value.x = (float) var.GetCVar<ubyte>();
        break;
      case ParameterVariant::VariantType::Bool:
        value.x = var.GetCVar<bool>() ? 1.0f : 0.0f;
        break;
      case ParameterVariant::VariantType::Vec2:
      {
        const Vec2 vec = var.GetCVar<Vec2>();
        value          = Vec4(vec.x, vec.y, 0.0f, 0.0f);
      }
      break;
      case ParameterVariant::VariantType::Vec3:
      {
        const Vec3 vec = var.GetCVar<Vec3>();
        value          = Vec4(vec, 0.0f);
      }
      break;
      case ParameterVariant::VariantType::Vec4:
        value = var.GetCVar<Vec4>();
        break;
      default:
        return false;
    }

    return true;
  }

  bool Animation::UnpackParamValue(ParameterVariant& var,
                                   ParameterVariant::VariantType type,
                                   const Vec4& value)
  {
    if (var.GetType() != type)
    {
      // A key that does not match the parameter it addresses is a corrupted or hand edited clip,
      // writing it would change the type of the parameter.
      return false;
    }

    // Assigning through the variant is what fires its change callbacks, which is how a material
    // cache or a light buffer learns about an animated value.
    switch (type)
    {
      case ParameterVariant::VariantType::Float:
        var = value.x;
        break;
      case ParameterVariant::VariantType::Int:
        var = (int) glm::round(value.x);
        break;
      case ParameterVariant::VariantType::UInt:
        var = (uint) glm::max(0.0f, glm::round(value.x));
        break;
      case ParameterVariant::VariantType::Byte:
        var = (byte) glm::round(value.x);
        break;
      case ParameterVariant::VariantType::Ubyte:
        var = (ubyte) glm::max(0.0f, glm::round(value.x));
        break;
      case ParameterVariant::VariantType::Bool:
        var = value.x != 0.0f;
        break;
      case ParameterVariant::VariantType::Vec2:
        var = Vec2(value.x, value.y);
        break;
      case ParameterVariant::VariantType::Vec3:
        var = Vec3(value.x, value.y, value.z);
        break;
      case ParameterVariant::VariantType::Vec4:
        var = value;
        break;
      default:
        return false;
    }

    return true;
  }

  bool Animation::SetParamKey(const String& trackName, int frame, const ParamKey* key)
  {
    if (trackName.empty())
    {
      return false;
    }

    ParamKeyArray* keys = m_paramKeys.Find(trackName);
    if (keys == nullptr)
    {
      if (key == nullptr)
      {
        return false;
      }

      m_paramKeys.Insert(trackName, ParamKeyArray());
      keys = m_paramKeys.Find(trackName);
    }

    auto it = std::lower_bound(keys->begin(),
                               keys->end(),
                               frame,
                               [](const ParamKey& k, int f) -> bool { return k.m_frame < f; });

    const bool found = (it != keys->end() && it->m_frame == frame);

    if (key == nullptr)
    {
      if (found)
      {
        keys->erase(it);
      }
    }
    else
    {
      // The stored frame is forced to match the sort position, a mismatched frame would read as a
      // corrupt curve.
      ParamKey newKey = *key;
      newKey.m_frame  = frame;

      if (found)
      {
        *it = newKey;
      }
      else
      {
        keys->insert(it, newKey);
      }
    }

    m_dirty = true;
    return true;
  }

  bool Animation::GetParamValue(const String& trackName,
                                float time,
                                Vec4& value,
                                ParameterVariant::VariantType& type) const
  {
    const ParamKeyArray* keys = m_paramKeys.Find(trackName);
    if (keys == nullptr || keys->empty())
    {
      return false;
    }

    int key1    = -1;
    int key2    = -1;
    float ratio = 0.0f;
    FindParamKeys(*keys, glm::max(1.0f, m_fps), time, key1, key2, ratio);

    if (key1 < 0 || key2 < 0)
    {
      return false;
    }

    const ParamKey& k1 = (*keys)[key1];
    const ParamKey& k2 = (*keys)[key2];

    type = k1.m_type;

    if (k1.m_type != k2.m_type || !IsInterpolatedParam(k1.m_type))
    {
      // A step type holds the key at or before the sampled time. Past the last key that is the last
      // key itself, which the sampler marks with ratio 1: holding k1 there would keep the value of
      // the key before it, so a bool track never reached its final state and a flag looked like it
      // was not animated at all.
      value = ratio >= 1.0f ? k2.m_value : k1.m_value;
      return true;
    }

    // glm::mix covers the four floats a ParamKey carries, whatever type they hold.
    value = glm::mix(k1.m_value, k2.m_value, ratio);
    return true;
  }

  ParameterVariant* Animation::ResolveParamTrack(EntityPtr entity, const String& trackName)
  {
    if (entity == nullptr || trackName.empty())
    {
      return nullptr;
    }

    // Every track id starts with the entity it belongs to, which is also how a transform track is
    // matched to its node.
    const String prefix = entity->GetNameVal() + ".";
    if (!StartsWith(trackName, prefix))
    {
      return nullptr;
    }

    const String rest = trackName.substr(prefix.length());
    if (rest.empty())
    {
      return nullptr;
    }

    // "<entity>.<param>": the entity's own parameter block.
    if (ParameterVariant* var = FindParam(entity->m_localData, rest))
    {
      return var;
    }

    // "<entity>.<componentClass>.<param>" and "<entity>.MaterialComponent.<index>.<param>".
    const size_t dot = rest.find('.');
    if (dot == String::npos)
    {
      return nullptr;
    }

    const String compName = rest.substr(0, dot);
    const String compRest = rest.substr(dot + 1);
    if (compRest.empty())
    {
      return nullptr;
    }

    for (ComponentPtr comp : entity->GetComponentPtrArray())
    {
      if (comp == nullptr || comp->Class()->Name != compName)
      {
        continue;
      }

      if (MaterialComponent* matComp = comp->As<MaterialComponent>())
      {
        // The material list index is the mesh / submesh index, so a slot is addressed by it.
        const size_t slotDot = compRest.find('.');
        if (slotDot == String::npos)
        {
          return nullptr;
        }

        const String slot   = compRest.substr(0, slotDot);
        const String paramN = compRest.substr(slotDot + 1);
        if (paramN.empty() || slot.empty() || slot.find_first_not_of("0123456789") != String::npos)
        {
          return nullptr;
        }

        const int index          = std::atoi(slot.c_str());
        MaterialPtrArray& mats   = matComp->GetMaterialList();
        if (index < 0 || index >= (int) mats.size() || mats[index] == nullptr)
        {
          return nullptr;
        }

        return FindParam(mats[index]->m_localData, paramN);
      }

      return FindParam(comp->m_localData, compRest);
    }

    return nullptr;
  }

  void Animation::ApplyParamTracks(EntityPtr entity, float time)
  {
    if (entity == nullptr || m_paramKeys.empty())
    {
      return;
    }

    for (const auto& track : m_paramKeys)
    {
      if (track.second.empty())
      {
        continue;
      }

      Vec4 value;
      ParameterVariant::VariantType type;
      if (!GetParamValue(track.first, time, value, type))
      {
        continue;
      }

      if (ParameterVariant* var = ResolveParamTrack(entity, track.first))
      {
        UnpackParamValue(*var, type, value);
      }
    }
  }

  void Animation::GetPose(Node* node, float time, const String& keyName)
  {
    if (m_keys.empty() || node == nullptr)
    {
      return;
    }

    // A clip carries one track per animated node, named after it, so a multi node clip has to be
    // asked for the curve that belongs to this node. Clips with a single unnamed curve keep working
    // through the fallback.
    const KeyArray* keys = keyName.empty() ? nullptr : m_keys.Find(keyName);
    if (keys == nullptr)
    {
      keys = &m_keys.begin()->second;
    }

    if (keys->empty())
    {
      return;
    }

    float ratio;
    int key1, key2;
    GetNearestKeys(*keys, key1, key2, ratio, time);

    if (key1 < 0 || key2 < 0 || key1 >= (int) keys->size() || key2 >= (int) keys->size())
    {
      return;
    }

    Key k1              = (*keys)[key1];
    Key k2              = (*keys)[key2];

    Vec3 positon        = Interpolate(k1.m_position, k2.m_position, ratio);
    Quaternion rotation = glm::slerp(k1.m_rotation, k2.m_rotation, ratio);
    Vec3 scale          = Interpolate(k1.m_scale, k2.m_scale, ratio);

    node->SetLocalTransforms(positon, rotation, scale);
  }

  void Animation::GetPose(const SkeletonComponentPtr& skeleton, float time)
  {
    if (m_keys.empty())
    {
      return;
    }

    float ratio;
    int key1, key2;
    Vec3 translation;
    Quaternion orientation;
    Vec3 scale;

    EntityPtr owner = skeleton->OwnerEntity();
    for (auto& dBoneIter : skeleton->m_map->m_boneMap)
    {
      KeyArray* keys = m_keys.Find(dBoneIter.first);
      if (keys == nullptr)
      {
        continue;
      }

      GetNearestKeys(*keys, key1, key2, ratio, time);

      // Sanity checks
      int keySize = static_cast<int>(keys->size());
      if (keySize <= key1 || keySize <= key2)
      {
        continue;
      }

      if (key1 == -1 || key2 == -1)
      {
        continue;
      }

      Key k1                             = (*keys)[key1];
      Key k2                             = (*keys)[key2];
      DynamicBoneMap::DynamicBone& dBone = dBoneIter.second;

      translation                        = Interpolate(k1.m_position, k2.m_position, ratio);
      orientation                        = glm::slerp(k1.m_rotation, k2.m_rotation, ratio);
      scale                              = Interpolate(k1.m_scale, k2.m_scale, ratio);

      // TODO CPU skinning for blended animations

      dBone.node->SetLocalTransforms(translation, orientation, scale);
    }
    skeleton->isDirty = true;
  }

  void Animation::GetPose(Node* node, int frame) { GetPose(node, frame * 1.0f / m_fps); }

  bool Animation::SetKey(const String& keyName, int frame, const Key* key)
  {
    if (keyName.empty())
    {
      return false;
    }

    KeyArray* keys = m_keys.Find(keyName);
    if (keys == nullptr)
    {
      if (key == nullptr)
      {
        return false;
      }

      m_keys.Insert(keyName, KeyArray());
      keys = m_keys.Find(keyName);
    }

    // Keys stay ascending by frame: GetNearestKeys walks the array in order and the anim data
    // texture path indexes it by keyframe, so a key is inserted at its sorted position.
    auto it = std::lower_bound(keys->begin(),
                               keys->end(),
                               frame,
                               [](const Key& k, int f) -> bool { return k.m_frame < f; });

    const bool found = (it != keys->end() && it->m_frame == frame);

    if (key == nullptr)
    {
      if (found)
      {
        keys->erase(it);
      }
    }
    else if (found)
    {
      *it = *key;
    }
    else
    {
      keys->insert(it, *key);
    }

    m_dirty = true;
    return true;
  }

  void Animation::Load()
  {
    if (!m_loaded)
    {
      ParseDocument("anim");
      m_loaded = true;
    }
  }

  XmlNode* Animation::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* container      = CreateXmlNode(doc, "anim", parent);

    char* fpsValueStr       = doc->allocate_string(std::to_string(m_fps).c_str());
    XmlAttribute* fpsAttrib = doc->allocate_attribute("fps", fpsValueStr);
    container->append_attribute(fpsAttrib);

    char* durationValueStr  = doc->allocate_string(std::to_string(m_duration).c_str());
    XmlAttribute* durAttrib = doc->allocate_attribute("duration", durationValueStr);
    container->append_attribute(durAttrib);

    if (!m_rootKey.empty())
    {
      XmlAttribute* rootAttrib = doc->allocate_attribute("rootKey", m_rootKey.c_str());
      container->append_attribute(rootAttrib);
    }

    for (const auto& [boneName, keys] : m_keys)
    {
      XmlNode* boneNode = CreateXmlNode(doc, "node", container);
      boneNode->append_attribute(doc->allocate_attribute(XmlNodeName.data(), boneName.c_str()));

      if constexpr (SERIALIZE_ANIMATION_AS_BINARY)
      {
        WriteAttr(boneNode, doc, "KeyCount", std::to_string(keys.size()));
        size_t keyBufferSize = keys.size() * sizeof(keys[0]);
        char* b64Data        = new char[keyBufferSize * 2];
        bintob64(b64Data, keys.data(), keyBufferSize);
        XmlNode* base64XML = CreateXmlNode(doc, "Base64", boneNode);
        base64XML->value(doc->allocate_string(b64Data));
        SafeDelArray(b64Data);
      }
      else
      {
        for (uint keyIndex = 0; keyIndex < keys.size(); keyIndex++)
        {
          XmlNode* keyNode         = CreateXmlNode(doc, "key", boneNode);
          const Key& key           = keys[keyIndex];

          char* frameIndexValueStr = doc->allocate_string(std::to_string(keyIndex).c_str());
          keyNode->append_attribute(doc->allocate_attribute("frame", frameIndexValueStr));

          WriteVec(CreateXmlNode(doc, "translation", keyNode), doc, key.m_position);

          WriteVec(CreateXmlNode(doc, "scale", keyNode), doc, key.m_scale);

          WriteVec(CreateXmlNode(doc, "rotation", keyNode), doc, key.m_rotation);
        }
      }
    }

    // Parameter tracks: one node per track, one child per key. Written as text rather than a packed
    // blob, a parameter track holds a handful of keys and the values stay readable in the file.
    for (const auto& [trackName, keys] : m_paramKeys)
    {
      if (keys.empty())
      {
        continue;
      }

      XmlNode* paramNode = CreateXmlNode(doc, "param", container);
      paramNode->append_attribute(doc->allocate_attribute(XmlNodeName.data(), trackName.c_str()));

      for (const ParamKey& key : keys)
      {
        XmlNode* keyNode = CreateXmlNode(doc, "key", paramNode);
        WriteAttr(keyNode, doc, "frame", std::to_string(key.m_frame));
        WriteAttr(keyNode, doc, "type", std::to_string((int) key.m_type));
        WriteVec(CreateXmlNode(doc, "value", keyNode), doc, key.m_value);
      }
    }

    return container;
  }

  XmlNode* Animation::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlAttribute* attr = parent->first_attribute("fps");
    m_fps              = (float) (std::atof(attr->value()));

    attr               = parent->first_attribute("duration");
    m_duration         = (float) (std::atof(attr->value()));

    if (XmlAttribute* rootKeyAttr = parent->first_attribute("rootKey"))
    {
      m_rootKey = rootKeyAttr->value();
    }
    else
    {
      m_rootKey.clear();
    }

    for (XmlNode* animNode = parent->first_node("node"); animNode; animNode = animNode->next_sibling())
    {
      attr            = animNode->first_attribute(XmlNodeName.data());
      String boneName = attr->value();

      // Keep the file order of the bone tracks on load.
      KeyArray* keys = m_keys.Find(boneName);
      if (keys == nullptr)
      {
        m_keys.Insert(boneName, KeyArray());
        keys = m_keys.Find(boneName);
      }

      // Serialized as base64
      if (XmlAttribute* keyCountAttr = animNode->first_attribute("KeyCount"))
      {
        uint keyCount = 0;
        ReadAttr(animNode, "KeyCount", keyCount);
        keys->resize(keyCount);
        XmlNode* b64Node = animNode->first_node("Base64");
        b64tobin(keys->data(), b64Node->value());
      }
      else
      {
        // Serialized as xml
        for (XmlNode* keyNode = animNode->first_node("key"); keyNode; keyNode = keyNode->next_sibling())
        {
          Key key;
          attr             = keyNode->first_attribute("frame");
          key.m_frame      = std::atoi(attr->value());

          XmlNode* subNode = keyNode->first_node("translation");
          ReadVec(subNode, key.m_position);

          subNode = keyNode->first_node("scale");
          ReadVec(subNode, key.m_scale);

          subNode = keyNode->first_node("rotation");
          ReadVec(subNode, key.m_rotation);

          keys->push_back(key);
        }
      }
    }

    // Parameter tracks. Clips that predate them simply have no <param> nodes.
    for (XmlNode* paramNode = parent->first_node("param"); paramNode; paramNode = paramNode->next_sibling("param"))
    {
      XmlAttribute* nameAttr = paramNode->first_attribute(XmlNodeName.data());
      if (nameAttr == nullptr)
      {
        continue;
      }

      const String trackName = nameAttr->value();

      ParamKeyArray* keys = m_paramKeys.Find(trackName);
      if (keys == nullptr)
      {
        m_paramKeys.Insert(trackName, ParamKeyArray());
        keys = m_paramKeys.Find(trackName);
      }

      for (XmlNode* keyNode = paramNode->first_node("key"); keyNode; keyNode = keyNode->next_sibling("key"))
      {
        ParamKey key;
        ReadAttr(keyNode, "frame", key.m_frame);

        int type = (int) ParameterVariant::VariantType::Float;
        ReadAttr(keyNode, "type", type);
        key.m_type = (ParameterVariant::VariantType) type;

        if (XmlNode* valueNode = keyNode->first_node("value"))
        {
          ReadVec(valueNode, key.m_value);
        }

        keys->push_back(key);
      }
    }

    return nullptr;
  }

  void Animation::Init(bool flushClientSideArray) { m_initiated = true; }

  void Animation::UnInit()
  {
    m_initiated = false;
    m_keys.clear();
    m_paramKeys.clear();
    m_rootKey.clear();
  }

  void Animation::CopyTo(Resource* other)
  {
    Super::CopyTo(other);
    Animation* cpy      = static_cast<Animation*>(other);
    cpy->m_keys         = m_keys;
    cpy->m_paramKeys    = m_paramKeys;
    cpy->m_fps          = m_fps;
    cpy->m_duration     = m_duration;
    cpy->m_rootKey      = m_rootKey;
  }

  void Animation::GetNearestKeys(const KeyArray& keys, int& key1, int& key2, float& ratio, float t)
  {
    // Find nearset keys.
    key1  = -1;
    key2  = -1;
    ratio = 0.0f;

    assert(keys.empty() != true && "Animation can't be empty !");

    // Check boundary cases.
    int keySize = static_cast<int>(keys.size());
    if (keySize == 1)
    {
      key1 = 0;
      key2 = 0;
      return;
    }

    // Current time is earliear than earliest time in the animation.
    float boundaryTime = keys.front().m_frame * 1.0f / m_fps;
    if (boundaryTime > t)
    {
      key1 = 0;
      key2 = 1;
      return;
    }

    // Current time is later than the latest time in the animation.
    boundaryTime = keys.back().m_frame * 1.0f / m_fps;
    if (t > boundaryTime)
    {
      key2  = keySize - 1;
      key1  = key2 - 1;
      ratio = 1.0f;
      return;
    }

    // Current time is in between keyframes.
    // Rote interpolation ratio and nearest keys.
    for (int i = 1; i < keySize; i++)
    {
      float keyTime2 = keys[i].m_frame / m_fps;
      float keyTime1 = keys[i - 1].m_frame / m_fps;

      if (t >= keyTime1 && keyTime2 >= t)
      {
        ratio = (t - keyTime1) / (keyTime2 - keyTime1);
        key1  = i - 1;
        key2  = i;
        return;
      }
    }
  }

  AnimRecord::AnimRecord() { m_id = GetObjectRegistry()->GenerateId(); }

  void AnimRecord::Construct(EntityPtr entity, AnimationPtr anim)
  {
    m_entity    = entity;
    m_animation = anim;
  }

  AnimRecord::~AnimRecord()
  {
    if (ObjectRegistry* registry = GetObjectRegistry())
    {
      registry->ReleaseId(m_id);
    }
  }

  AnimationPlayer::AnimationPlayer() {}

  AnimationPlayer::~AnimationPlayer() { Destroy(); }

  void AnimationPlayer::Destroy()
  {
    ClearAnimRecords();
    ClearAnimationData();
  }

  void AnimationPlayer::ClearAnimRecords()
  {
    // Remove circular dependency
    for (AnimRecordPtr animRecord : m_records)
    {
      animRecord->m_blendingData.recordToBlend     = nullptr;
      animRecord->m_blendingData.recordToBeBlended = nullptr;
    }
    m_records.clear();
  }

  AnimRecordPtrArray AnimationPlayer::GetRecords() { return m_records; }

  void AnimationPlayer::AddRecord(AnimRecordPtr rec)
  {
    int indx = Exist(rec->m_id);
    if (indx != -1)
    {
      return;
    }

    // If recoding already exists, do not add it again
    bool exist = false;
    for (AnimRecordPtr animRecord : m_records)
    {
      if (animRecord == rec)
      {
        exist = true;
        break;
      }
    }

    // Generate animation frame data
    AddAnimationData(rec->m_entity, rec->m_animation);

    if (!exist)
    {
      m_records.push_back(rec);
    }
  }

  void AnimationPlayer::RemoveRecord(ObjectId id)
  {
    int indx = Exist(id);
    if (indx != -1)
    {
      auto it = m_records.begin() + indx;
      m_records.erase(it);

      UpdateAnimationData();
    }
  }

  void AnimationPlayer::RemoveRecord(const AnimRecord& rec) { RemoveRecord(rec.m_id); }

  void AnimationPlayer::Pause() { m_paused = true; }

  void AnimationPlayer::Resume() { m_paused = false; }

  bool AnimationPlayer::IsPaused() const { return m_paused; }

  void AnimationPlayer::Update(float deltaTimeSec)
  {
    // While paused, Update advances no record at all: record times, blend
    // countdowns and root motion stay exactly where they are, and the animation
    // data of the last update is kept untouched, so whatever reads it does not
    // change. No time is skipped while paused and none is replayed on resume.
    if (m_paused)
    {
      return;
    }

    // Updates all the records in the player and returns true if record needs to be removed.
    auto updateRecordsFn = [&](AnimRecordPtr record) -> bool
    {
      if (record->m_state == AnimRecord::State::Pause)
      {
        return false;
      }

      AnimRecord::State state = record->m_state;
      if (state == AnimRecord::State::Play)
      {
        record->m_currentTime += (deltaTimeSec * record->m_timeMultiplier * m_timeMultiplier);
        float duration         = record->m_animation->m_duration;
        if (record->m_loop)
        {
          float leftOver = record->m_currentTime - duration;
          if (leftOver > 0.0)
          {
            record->m_currentTime = leftOver;
          }
        }
        else
        {
          // One-shot clip: play once, then hold its final frame. The record
          // stays registered (no further root motion once the pose holds)
          // until another Play replaces it or Stop is called -- it never wraps
          // back to its first frame and never vanishes mid-blend.
          if (record->m_currentTime > duration)
          {
            record->m_currentTime = duration;
          }
        }

        if (record->m_blendingData.recordToBeBlended != nullptr)
        {
          record->m_blendingData.blendCurrentDurationInSec -=
              deltaTimeSec * record->m_timeMultiplier * m_timeMultiplier;

          if (record->m_blendingData.blendCurrentDurationInSec < 0.0)
          {
            // The outgoing clip finished its fade-out and is dropped from the
            // player.
            return true;
          }
        }
      }

      if (state == AnimRecord::State::Rewind)
      {
        record->m_currentTime        = 0.0f;
        record->m_prevRootMotionTime = 0.0f;
      }

      if (state == AnimRecord::State::Rewind)
      {
        record->m_state = AnimRecord::State::Play;
      }

      return state == AnimRecord::State::Stop;
    };

    // Update all active animation records
    bool anyAnimRecordDeleted = false;
    for (AnimRecordPtrArray::iterator it = m_records.begin(); it != m_records.end();)
    {
      if (updateRecordsFn(*it))
      {
        // remove record from both blending map and records array

        anyAnimRecordDeleted = true;

        if (EntityPtr ntt = (*it)->m_entity.lock())
        {
          if (SkeletonComponentPtr skComp = ntt->GetComponent<SkeletonComponent>())
          {
            skComp->m_animData.currentAnimation = nullptr;
            skComp->m_animData.blendAnimation   = nullptr;
          }
        }

        // Remove blending record from record to be blended
        if ((*it)->m_blendingData.recordToBeBlended != nullptr)
        {
          (*it)->m_blendingData.recordToBeBlended->m_blendingData.recordToBlend = nullptr;
        }

        it = m_records.erase(it);
      }
      else
      {
        ++it;
      }
    }

    // remove unused animation data textures
    if (anyAnimRecordDeleted)
    {
      UpdateAnimationData();
    }

    // Fill skeleton components with anim data and pose plain entity nodes.
    for (auto it = m_records.begin(); it != m_records.end(); it++)
    {
      AnimRecordPtr record = *it;

      if (EntityPtr ntt = record->m_entity.lock())
      {
        MeshComponentPtr meshComp   = ntt->GetMeshComponent();
        SkeletonComponentPtr skComp = ntt->GetComponent<SkeletonComponent>();
        MeshPtr mesh                = meshComp != nullptr ? meshComp->GetMeshVal() : nullptr;

        // Parameter tracks of the clip are applied for the record's entity, next to the pose below.
        record->m_animation->ApplyParamTracks(ntt, record->m_currentTime);

        if (mesh != nullptr && mesh->IsSkinned() && skComp != nullptr)
        {
          assert(record->m_animation->m_keys.size() > 0);
          KeyArray& keys = (*(record->m_animation->m_keys.begin())).second;
          int key1, key2;
          float ratio;
          record->m_animation->GetNearestKeys(keys, key1, key2, ratio, record->m_currentTime);

          skComp->m_animData.keyFrameCount             = (float) keys.size();
          skComp->m_animData.firstKeyFrame             = (float) key1 / skComp->m_animData.keyFrameCount;
          skComp->m_animData.secondKeyFrame            = (float) key2 / skComp->m_animData.keyFrameCount;
          skComp->m_animData.keyFrameInterpolationTime = ratio;
          skComp->m_animData.currentAnimation          = record->m_animation;

          AnimRecordPtr recordToBlend                  = record->m_blendingData.recordToBlend;
          if (recordToBlend != nullptr)
          {
            KeyArray& blendAnimKeys = (*(recordToBlend->m_animation->m_keys.begin())).second;
            recordToBlend->m_animation->GetNearestKeys(blendAnimKeys, key1, key2, ratio, recordToBlend->m_currentTime);

            skComp->m_animData.blendKeyFrameCount   = (float) blendAnimKeys.size();
            skComp->m_animData.animationBlendFactor = recordToBlend->m_blendingData.blendCurrentDurationInSec /
                                                      recordToBlend->m_blendingData.blendTotalDurationInSec;
            skComp->m_animData.blendFirstKeyFrame   = (float) key1 / skComp->m_animData.blendKeyFrameCount;
            skComp->m_animData.blendSecondKeyFrame  = (float) key2 / skComp->m_animData.blendKeyFrameCount;
            skComp->m_animData.blendKeyFrameInterpolationTime = ratio;
            skComp->m_animData.blendAnimation                 = recordToBlend->m_animation;
          }
          else
          {
            skComp->m_animData.blendAnimation = nullptr;
          }
        }
        else if (!record->m_applyRootMotion)
        {
          // Node animation: the clip's track named after the entity carries its curve, so the
          // entity node is posed directly. Records that ask for root motion stay under root motion
          // control (applied below), which accumulates deltas on the node instead of setting it.
          //
          // Entity::SetPose() would do the same, but it also applies the parameter tracks, which
          // this loop already did above for every record.
          record->m_animation->GetPose(ntt->m_node, record->m_currentTime, ntt->GetNameVal());
        }
      }
    }

    // Apply root motion for records that request it.
    for (AnimRecordPtr record : m_records)
    {
      if (record->m_state == AnimRecord::State::Play && record->m_applyRootMotion)
      {
        ApplyRootMotion(record);
      }
    }
  }

  void AnimationPlayer::ApplyRootMotion(AnimRecordPtr record)
  {
    AnimationPtr anim = record->m_animation;
    if (anim == nullptr)
    {
      return;
    }

    EntityPtr ntt = record->m_entity.lock();
    if (ntt == nullptr)
    {
      return;
    }

    const String& rootKey = anim->m_rootKey;
    if (rootKey.empty())
    {
      return;
    }

    const KeyArray* keys = anim->m_keys.Find(rootKey);
    if (keys == nullptr)
    {
      return;
    }

    auto sampleKey = [anim](const KeyArray& keys, float time, Vec3& pos, Quaternion& rot, Vec3& scale) -> void
    {
      int key1, key2;
      float ratio;
      anim->GetNearestKeys(keys, key1, key2, ratio, time);

      if (key1 < 0 || key2 < 0)
      {
        return;
      }

      Key k1 = keys[key1];
      Key k2 = keys[key2];
      pos    = Interpolate(k1.m_position, k2.m_position, ratio);
      rot    = glm::slerp(k1.m_rotation, k2.m_rotation, ratio);
      scale  = Interpolate(k1.m_scale, k2.m_scale, ratio);
    };

    Vec3 curPos, prevPos, curScale, prevScale;
    Quaternion curRot, prevRot;

    const float curTime = record->m_currentTime;
    float prevTime      = record->m_prevRootMotionTime;

    // At the loop boundary the root curve jumps back to the start of the cycle
    // (e.g. from x 30 back to x 0). That jump is not real motion; measuring the
    // delta between the cycle end and the cycle start would reverse all
    // accumulated displacement. Measure against the cycle start pose instead so
    // the character keeps accumulating forward from there.
    if (curTime < prevTime)
    {
      prevTime = 0.0f;
    }

    sampleKey(*keys, curTime, curPos, curRot, curScale);
    sampleKey(*keys, prevTime, prevPos, prevRot, prevScale);

    Vec3 deltaPos = curPos - prevPos;

    Quaternion deltaRot = curRot * glm::inverse(prevRot);

    Vec3 deltaScale(1.0f);
    for (int i = 0; i < 3; i++)
    {
      if (glm::abs(prevScale[i]) > 0.0001f)
      {
        deltaScale[i] = curScale[i] / prevScale[i];
      }
    }

    // Apply the root key's displacement to the entity node in its local
    // space. The root curve is authored along the character's own axes, so the
    // displacement must follow the node's (and its ancestors') orientation:
    // rotating the actor -- e.g. the prefab's top root -- points the root
    // motion at the direction the actor should walk, instead of always pushing
    // it along the fixed world axis the curve was baked on.
    ntt->m_node->Translate(deltaPos, TransformationSpace::TS_LOCAL);
    ntt->m_node->Rotate(deltaRot, TransformationSpace::TS_LOCAL);
    ntt->m_node->Scale(deltaScale);

    record->m_prevRootMotionTime = curTime;
  }

  int AnimationPlayer::Exist(ObjectId id) const
  {
    for (size_t i = 0; i < m_records.size(); i++)
    {
      if (m_records[i]->m_id == id)
      {
        return static_cast<int>(i);
      }
    }

    return -1;
  }

  DataTexturePtr AnimationPlayer::GetAnimationDataTexture(ObjectId skelID, ObjectId animID)
  {
    const std::pair<ObjectId, ObjectId> p = std::make_pair(skelID, animID);
    if (m_animTextures.find(p) != m_animTextures.end())
    {
      return m_animTextures[p];
    }

    return nullptr;
  }

  void AnimationPlayer::AddAnimationData(EntityWeakPtr ntt, AnimationPtr anim)
  {
    if (EntityPtr entity = ntt.lock())
    {
      if (SkeletonComponentPtr skelComp = entity->GetComponent<SkeletonComponent>())
      {
        if (SkeletonPtr skeleton = skelComp->GetSkeletonResourceVal())
        {
          if (m_animTextures.find(std::make_pair(skeleton->GetIdVal(), anim->GetIdVal())) != m_animTextures.end())
          {
            // this animation data already exists
            return;
          }

          DataTexturePtr texture = CreateAnimationDataTexture(skeleton, anim);
          m_animTextures[std::make_pair(skeleton->GetIdVal(), anim->GetIdVal())] = texture;
        }
      }
    }
  }

  void AnimationPlayer::UpdateAnimationData()
  {
    std::map<std::pair<ObjectId, ObjectId>, DataTexturePtr>::iterator it;
    for (it = m_animTextures.begin(); it != m_animTextures.end();)
    {
      bool found = false;
      for (AnimRecordPtr animRecord : m_records)
      {
        if (EntityPtr entity = animRecord->m_entity.lock())
        {
          if (SkeletonComponentPtr skelComp = entity->GetComponent<SkeletonComponent>())
          {
            if (SkeletonPtr skeleton = skelComp->GetSkeletonResourceVal())
            {
              const ObjectId skeletonID = skeleton->GetIdVal();
              const ObjectId animID     = animRecord->m_animation->GetIdVal();

              if (it->first.first == skeletonID && it->first.second == animID)
              {
                found = true;
                break;
              }
            }
          }
        }
      }

      if (found)
      {
        ++it;
      }
      else
      {
        it = m_animTextures.erase(it);
      }
    }
  }

  void AnimationPlayer::ClearAnimationData() { m_animTextures.clear(); }

  DataTexturePtr AnimationPlayer::CreateAnimationDataTexture(SkeletonPtr skeleton, AnimationPtr anim)
  {
    if (anim->m_keys.empty())
    {
      return nullptr;
    }

    uint height        = 1024;                               // max number of key frames
    uint width         = (int) skeleton->m_bones.size() * 4; // number of bones * 4 (each element holds a row of matrix)
    uint sizeOfElement = 16 * 4;                             // size of an element in bytes

    char* buffer       = new char[height * width * sizeOfElement];

    uint maxKeyCount   = 0;
    uint keyframeIndex = 0;
    while (true)
    {
      if (keyframeIndex >= height)
      {
        TK_ERR("The maximum number of key frames for animations is 1024!");
        TK_ERR("Animation \"%s\" has more than 1024 key frames.", anim->GetFile().c_str());
        SafeDelArray(buffer);
        return nullptr;
      }

      bool keysframesLeft = false;
      std::vector<std::pair<Node*, uint>> boneNodes;

      // Iterate all bones for the key frame and get node transformations
      for (auto& dBoneIter : skeleton->m_Tpose.m_boneMap)
      {
        const String& name                 = dBoneIter.first;
        DynamicBoneMap::DynamicBone& dBone = dBoneIter.second;

        KeyArray* keys = anim->m_keys.Find(name);
        if (keys == nullptr)
        {
          dBone.node->SetLocalTransforms(Vec3(), Quaternion(), Vec3(1.0f));
          boneNodes.push_back(std::make_pair(dBone.node, dBone.boneIndx));
          continue;
        }

        std::vector<Key>& keyArray = *keys;
        if (keyArray.size() <= keyframeIndex)
        {
          continue;
        }
        else
        {
          if (maxKeyCount < keyArray.size())
          {
            maxKeyCount = (uint) keyArray.size();
          }

          keysframesLeft = true;

          Key& key       = keyArray[keyframeIndex];
          dBone.node->SetLocalTransforms(key.m_position, key.m_rotation, key.m_scale);
          boneNodes.push_back(std::make_pair(dBone.node, dBone.boneIndx));
        }
      }

      if (!keysframesLeft)
      {
        break;
      }

      // After getting all node transformations re-calculate dirty nodes transformations
      for (auto& node : boneNodes)
      {
        StaticBone* sBone         = skeleton->m_bones[node.second];

        const Mat4 boneTransform  = node.first->GetTransform(TransformationSpace::TS_WORLD);
        const Mat4 totalTransform = boneTransform * sBone->m_inverseWorldMatrix;

        uint loc                  = ((keyframeIndex * (uint) skeleton->m_bones.size() + node.second) * sizeOfElement);
        memcpy(buffer + loc, &totalTransform, sizeOfElement);
      }

      ++keyframeIndex;
    }

    TextureSettings dataTextureSettings;
    dataTextureSettings.Target         = GraphicTypes::Target2D;
    dataTextureSettings.WarpS          = GraphicTypes::UVClampToEdge;
    dataTextureSettings.WarpT          = GraphicTypes::UVClampToEdge;
    dataTextureSettings.WarpR          = GraphicTypes::UVClampToEdge;
    dataTextureSettings.InternalFormat = GraphicTypes::FormatRGBA32F;
    dataTextureSettings.Format         = GraphicTypes::FormatRGBA;
    dataTextureSettings.Type           = GraphicTypes::TypeFloat;
    DataTexturePtr animDataTexture     = MakeNewPtr<DataTexture>(width, maxKeyCount, dataTextureSettings);
    animDataTexture->Init((void*) buffer);

    SafeDelArray(buffer);

    return animDataTexture;
  }

  AnimationManager::AnimationManager() { m_baseType = Animation::StaticClass(); }

  AnimationManager::~AnimationManager() {}

  bool AnimationManager::CanStore(ClassMeta* Class) { return Class == Animation::StaticClass(); }

} // namespace ToolKit
