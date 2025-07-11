/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
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
    ParamRecords().GetVar<AnimRecordPtrMap>().insert(std::make_pair(signalName, record));
  }

  void AnimControllerComponent::RemoveSignal(const String& signalName)
  {
    const auto& signal = GetRecordsVal().find(signalName);
    if (signal == GetRecordsVal().end())
    {
      return;
    }

    GetAnimationPlayer()->RemoveRecord(signal->second->m_id);
    ParamRecords().GetVar<AnimRecordPtrMap>().erase(signalName);
  }

  void AnimControllerComponent::SmoothTransition(const String& nextAnimName, float transitionDuration)
  {
    AnimRecordPtr lastActiveRecord = activeRecord;

    Play(nextAnimName, false);

    if (activeRecord != nullptr && lastActiveRecord != nullptr)
    {
      // set blending data

      // check if they have same bones
      assert(HaveSameKeys(activeRecord->m_animation->m_keys, lastActiveRecord->m_animation->m_keys) &&
             "Blend animation is for different skeleton than the animation to blend with!");

      activeRecord->m_blendingData.recordToBlend                 = lastActiveRecord;
      lastActiveRecord->m_blendingData.recordToBlend             = nullptr;
      lastActiveRecord->m_blendingData.blendCurrentDurationInSec = transitionDuration;
      lastActiveRecord->m_blendingData.blendTotalDurationInSec   = transitionDuration;
      lastActiveRecord->m_blendingData.recordToBeBlended         = activeRecord;
    }
  }

  void AnimControllerComponent::Play(const String& signalName, bool stopPrevAnim)
  {
    AnimRecordPtrMap& list = ParamRecords().GetVar<AnimRecordPtrMap>();
    AnimRecordPtr& rec     = list[signalName];
    if (rec == nullptr)
    {
      return;
    }

    if (activeRecord && stopPrevAnim)
    {
      activeRecord->m_state = AnimRecord::State::Stop;
    }
    rec->m_currentTime                    = 0.0f;
    rec->m_state                          = AnimRecord::State::Play;
    rec->m_loop                           = true;
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

  AnimRecordPtr AnimControllerComponent::GetActiveRecord() { return activeRecord; }

  AnimRecordPtr AnimControllerComponent::GetAnimRecord(const String& signalName)
  {
    AnimRecordPtrMap& records = ParamRecords().GetVar<AnimRecordPtrMap>();
    const auto& recordIter    = records.find(signalName);
    if (recordIter != records.end())
    {
      return recordIter->second;
    }
    return nullptr;
  }

} // namespace ToolKit