#include "EditorEnvironmentComponent.h"

#include <RenderSystem.h>
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
      FireCaptureInvalidate();
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
      TKAsyncTask(WorkerManager::MainThread,
                  [this]() -> void
                  {
                    if (!m_captureTimerActive)
                    {
                      return;
                    }

                    float elapsed = GetTiming()->CurrentTime - m_captureTimerStart;
                    if (elapsed >= CaptureDebounceTime)
                    {
                      m_captureTimerActive = false;
                      CaptureEnvironment();
                    }
                    else
                    {
                      // Not expired yet, re-enqueue for the next frame.
                      ScheduleCapturePollingTask();
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
