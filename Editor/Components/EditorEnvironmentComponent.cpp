#include "EditorEnvironmentComponent.h"

#include <ReflectionProbe.h>
#include <RenderSystem.h>
#include <Sky.h>
#include <Threads.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(EditorEnvironmentComponent, EnvironmentComponent);

    EditorEnvironmentComponent::EditorEnvironmentComponent() {}

    EditorEnvironmentComponent::~EditorEnvironmentComponent() {}

    void EditorEnvironmentComponent::InvalidateSpatialCaches()
    {
      Super::InvalidateSpatialCaches();

      // Sky manages its own irradiance, never auto-capture for it.
      EntityPtr owner = OwnerEntity();
      if (owner != nullptr && owner->IsA<SkyBase>())
      {
        return;
      }

      if (HdriPtr hdr = GetHdriVal())
      {
        // Only trigger capture for dynamic resources.
        if (hdr->IsDynamic())
        {
          FireCaptureInvalidate();
        }
      }
    }

    void EditorEnvironmentComponent::FireCaptureInvalidate()
    {
      m_captureTimerStart = GetTiming()->CurrentTime;

      if (m_captureTimerActive)
      {
        // Timer already running, just reset the start time (debounce restart).
        return;
      }

      m_captureTimerActive = true;

      // Schedule a polling task that re-enqueues itself each frame until the timer expires.
      ScheduleCapturePollingTask();
    }

    void EditorEnvironmentComponent::ScheduleCapturePollingTask()
    {
      // The task outlives the frame it is queued in and re-enqueues itself, so it can run after
      // the component is gone: deleting the entity, reloading the scene or undoing the edit that
      // started the timer all destroy it while a task is still in the main thread queue. A raw
      // this there reads freed memory, so the task holds a weak reference and leaves when the
      // component it was polling for no longer exists.
      EditorEnvironmentComponentWeakPtr weakSelf = Self<EditorEnvironmentComponent>();

      TKAsyncTask(WorkerManager::MainThread,
                  [weakSelf]() -> void
                  {
                    EditorEnvironmentComponentPtr self = weakSelf.lock();
                    if (self == nullptr || !self->m_captureTimerActive)
                    {
                      return;
                    }

                    float elapsed = GetTiming()->CurrentTime - self->m_captureTimerStart;
                    if (elapsed >= CaptureDebounceTime)
                    {
                      self->m_captureTimerActive = false;
                      self->CaptureEnvironment();
                    }
                    else
                    {
                      // Not expired yet, re-enqueue for the next frame.
                      self->ScheduleCapturePollingTask();
                    }
                  });
    }

    ComponentPtr EditorEnvironmentComponent::Copy(EntityPtr ntt) { return Super::Copy(ntt); }

    void EditorEnvironmentComponent::ParameterConstructor()
    {
      Super::ParameterConstructor();

      Capture_Define([this]() -> void { CaptureEnvironment(); },
                     EnvironmentComponentCategory.Name,
                     EnvironmentComponentCategory.Priority,
                     true,
                     true);

      CenterToEnvironment_Define(
          [this]() -> void
          {
            EntityPtr owner = OwnerEntity();
            if (owner == nullptr)
            {
              return;
            }

            Vec3 offset = GetPositionOffsetVal();
            if (offset == Vec3(0.0f))
            {
              return;
            }

            Mat4 worldTransform = owner->m_node->GetTransform(TransformationSpace::TS_WORLD);
            Vec3 worldCenter    = Vec3(worldTransform * Vec4(offset, 1.0f));

            owner->m_node->SetTranslation(worldCenter, TransformationSpace::TS_WORLD);
            SetPositionOffsetVal(Vec3(0.0f));
          },
          EnvironmentComponentCategory.Name,
          EnvironmentComponentCategory.Priority,
          true,
          true);
    }

    void EditorEnvironmentComponent::ParameterEventConstructor()
    {
      Super::ParameterEventConstructor();

      // Three values reach this component without passing through its owner entity: a capture
      // replaces the Hdri, and the box edit tool writes Size and PositionOffset straight onto the
      // component. A reflection probe mirrors these into its own parameter group, which is what the
      // inspector shows, so they are mirrored back here - otherwise the entity keeps the value from
      // before the capture or the resize, and the next edit of any parameter would carry that stale
      // value back into the component. SyncFromComponent guards itself, so the refresh does not
      // write back into the component.
      const auto mirror = [this](Value&, Value&) -> void
      {
        EntityPtr owner = OwnerEntity();
        if (owner == nullptr)
        {
          return;
        }

        if (ReflectionProbe* probe = owner->As<ReflectionProbe>())
        {
          probe->SyncFromComponent();
        }
      };

      ParamHdri().m_onValueChangedFn.push_back(mirror);
      ParamSize().m_onValueChangedFn.push_back(mirror);
      ParamPositionOffset().m_onValueChangedFn.push_back(mirror);
    }

    XmlNode* EditorEnvironmentComponent::SerializeImp(XmlDocument* doc, XmlNode* parent) const
    {
      return Super::SerializeImp(doc, parent);
    }

    XmlNode* EditorEnvironmentComponent::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      return Super::DeSerializeImp(info, parent);
    }

  } // namespace Editor
} // namespace ToolKit
