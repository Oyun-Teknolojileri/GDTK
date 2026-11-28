/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "AnimationControllerComponent.h"

#include "Animation.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  TKDefineClass(AnimControllerComponent, Component);

  AnimControllerComponent::AnimControllerComponent() {}

  AnimControllerComponent::~AnimControllerComponent()
  {
    if (activeRecord != nullptr)
    {
      GetAnimationPlayer()->RemoveRecord(activeRecord->m_id);
    }
  }

  ComponentPtr AnimControllerComponent::Copy(EntityPtr ntt)
  {
    AnimControllerComponentPtr ec = MakeNewPtr<AnimControllerComponent>();
    ec->m_localData               = m_localData;
    ec->m_entity                  = ntt;

    for (auto& record : ec->ParamRecords().GetVar<AnimRecordPtrMap>())
    {
      AnimRecordPtr newRecord = MakeNewPtr<AnimRecord>();
      ObjectId p_id           = newRecord->m_id;
      *newRecord              = *record.second;
      newRecord->m_id         = p_id;
      newRecord->m_entity     = ntt;
      record.second           = newRecord;
    }

    return ec;
  }

  void AnimControllerComponent::ParameterConstructor()
  {
    Super::ParameterConstructor();
    Records_Define({}, AnimRecordComponentCategory.Name, AnimRecordComponentCategory.Priority, true, true);
  }

  XmlNode* AnimControllerComponent::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlNode* compNode      = Super::DeSerializeImp(info, parent);
    AnimRecordPtrMap& list = ParamRecords().GetVar<AnimRecordPtrMap>();
    for (auto iter = list.begin(); iter != list.end(); ++iter)
    {
      iter->second->m_entity = OwnerEntity();
    }

    return compNode->first_node(StaticClass()->Name.c_str());
  }

  XmlNode* AnimControllerComponent::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    if (!m_serializableComponent)
    {
      return root;
    }

    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

  void AnimControllerComponent::AddSignal(const String& signalName, AnimRecordPtr record)
  {
    AnimRecordPtrMap& list = ParamRecords().GetVar<AnimRecordPtrMap>();
    list.Insert(signalName, record); // Ignores duplicate signal names.
  }

  void AnimControllerComponent::RemoveSignal(const String& signalName)
  {
    AnimRecordPtrMap& list = ParamRecords().GetVar<AnimRecordPtrMap>();
    if (AnimRecordPtr* record = list.Find(signalName))
    {
      GetAnimationPlayer()->RemoveRecord((*record)->m_id);
    }
    list.Erase(signalName);
  }

  void AnimControllerComponent::SmoothTransition(const String& nextAnimName, float transitionDuration)
  {
    AnimRecordPtr lastActiveRecord = activeRecord;

    Play(nextAnimName, false);

    // Nothing to fade: no record was playing, the name resolved to the record that is already
    // playing, or the lookup failed and left the current record in place.
    if (activeRecord == nullptr || lastActiveRecord == nullptr || activeRecord == lastActiveRecord)
    {
      return;
    }

    // A zero length blend is a hard cut, and the factor it would compute divides by zero.
    if (transitionDuration <= 0.0f)
    {
      return;
    }

    // The clips are not compared track by track on purpose. AnimationPlayer writes one animation
    // data texture per (skeleton, clip) pair, and a texture holds a row for every bone of the
    // skeleton -- a bone the clip has no track for keeps its bind pose (see
    // CreateAnimationDataTexture). The two textures of a blend are therefore lined up bone by bone
    // even when the clips animate different subsets of the rig, which is the normal case for a
    // library where each clip carries only the tracks it moves. The old HaveSameKeys() assert
    // rejected exactly that case and made such clips impossible to cross fade.
    if (activeRecord->m_animation == nullptr || activeRecord->m_animation->m_keys.empty() ||
        lastActiveRecord->m_animation == nullptr || lastActiveRecord->m_animation->m_keys.empty())
    {
      return;
    }

    // Set the blending data. The outgoing record counts the transition down and is dropped from
    // the animation player when it reaches zero, while the incoming record shows both poses.
    activeRecord->m_blendingData.recordToBlend                 = lastActiveRecord;
    lastActiveRecord->m_blendingData.recordToBlend             = nullptr;
    lastActiveRecord->m_blendingData.blendCurrentDurationInSec = transitionDuration;
    lastActiveRecord->m_blendingData.blendTotalDurationInSec   = transitionDuration;
    lastActiveRecord->m_blendingData.recordToBeBlended         = activeRecord;
  }

  void AnimControllerComponent::Play(const String& signalName, bool stopPrevAnim)
  {
    AnimRecordPtrMap& list = ParamRecords().GetVar<AnimRecordPtrMap>();
    AnimRecordPtr* recPtr  = list.Find(signalName);
    if (recPtr == nullptr)
    {
      return;
    }

    AnimRecordPtr rec = *recPtr;
    if (activeRecord && stopPrevAnim)
    {
      activeRecord->m_state = AnimRecord::State::Stop;
    }
    rec->m_currentTime                    = 0.0f;
    rec->m_prevRootMotionTime             = 0.0f;
    rec->m_state                          = AnimRecord::State::Play;
    // Respect the record's own loop flag: callers mark looping clips (idle,
    // walk cycles) with m_loop = true and one-shot clips (walk_f_start,
    // walk_f_end) with m_loop = false, which play once and hold their final
    // frame (see AnimationPlayer::Update). Forcing m_loop here used to make
    // every clip -- including one-shots -- wrap back to its first frame.
    rec->m_blendingData.recordToBlend     = nullptr;
    rec->m_blendingData.recordToBeBlended = nullptr;
    rec->m_entity                         = OwnerEntity();
    activeRecord                          = rec;
    GetAnimationPlayer()->AddRecord(rec);
  }

  void AnimControllerComponent::Stop()
  {
    if (activeRecord)
    {
      activeRecord->m_state = AnimRecord::State::Stop;
      activeRecord          = nullptr;
    }
  }

  void AnimControllerComponent::Pause() { activeRecord->m_state = AnimRecord::State::Pause; }

  void AnimControllerComponent::Resume()
  {
    if (activeRecord == nullptr || activeRecord->m_state != AnimRecord::State::Pause)
    {
      return;
    }

    // Keep the current clip time and pose; only resume the playback state.
    activeRecord->m_state  = AnimRecord::State::Play;
    activeRecord->m_entity = OwnerEntity();
    GetAnimationPlayer()->AddRecord(activeRecord);
  }

  AnimRecordPtr AnimControllerComponent::GetActiveRecord() { return activeRecord; }

  AnimRecordPtr AnimControllerComponent::GetAnimRecord(const String& signalName)
  {
    AnimRecordPtrMap& records = ParamRecords().GetVar<AnimRecordPtrMap>();
    if (AnimRecordPtr* record = records.Find(signalName))
    {
      return *record;
    }
    return nullptr;
  }

} // namespace ToolKit