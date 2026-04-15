#include "EditorReflectionProbe.h"

#include <EnvironmentComponent.h>
#include <RenderSystem.h>
#include <Threads.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(EditorReflectionProbe, ReflectionProbe);

    EditorReflectionProbe::EditorReflectionProbe() {}

    EditorReflectionProbe::~EditorReflectionProbe() {}

    ObjectPtr EditorReflectionProbe::Copy() const { return Super::Copy(); }

    void EditorReflectionProbe::FireCaptureInvalidate()
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

    void EditorReflectionProbe::ScheduleCapturePollingTask()
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

    void EditorReflectionProbe::ParameterConstructor()
    {
      Super::ParameterConstructor();

      Capture_Define([this]() -> void { CaptureEnvironment(); },
                     ReflectionProbeCategory.Name,
                     ReflectionProbeCategory.Priority,
                     true,
                     true);

      CenterToVolume_Define(
          [this]() -> void
          {
            EnvironmentComponentPtr envComp = GetEnvironmentComponent();
            if (envComp == nullptr)
            {
              return;
            }

            Vec3 offset = envComp->GetPositionOffsetVal();
            if (offset == Vec3(0.0f))
            {
              return;
            }

            Mat4 worldTransform = m_node->GetTransform(TransformationSpace::TS_WORLD);
            Vec3 worldCenter    = Vec3(worldTransform * Vec4(offset, 1.0f));

            m_node->SetTranslation(worldCenter, TransformationSpace::TS_WORLD);
            envComp->SetPositionOffsetVal(Vec3(0.0f));
          },
          ReflectionProbeCategory.Name,
          ReflectionProbeCategory.Priority,
          true,
          true);
    }

    void EditorReflectionProbe::ParameterEventConstructor() { Super::ParameterEventConstructor(); }

    XmlNode* EditorReflectionProbe::SerializeImp(XmlDocument* doc, XmlNode* parent) const
    {
      return Super::SerializeImp(doc, parent);
    }

    XmlNode* EditorReflectionProbe::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      return Super::DeSerializeImp(info, parent);
    }

  } // namespace Editor
} // namespace ToolKit
