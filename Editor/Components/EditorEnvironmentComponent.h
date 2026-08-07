#pragma once

#include "EnvironmentComponent.h"

#include <ToolKit.h>
#include <Types.h>

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API EditorEnvironmentComponent : public EnvironmentComponent
    {
     public:
      TKDeclareClass(EditorEnvironmentComponent, EnvironmentComponent);

      EditorEnvironmentComponent();
      virtual ~EditorEnvironmentComponent();
      ComponentPtr Copy(EntityPtr ntt) override;

      void InvalidateSpatialCaches() override;

      /** Starts or restarts a debounce timer. When the timer expires, CaptureEnvironment is triggered. */
      void FireCaptureInvalidate();

     protected:
      XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
      XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

     private:
      void ParameterConstructor() override;
      void ScheduleCapturePollingTask();

     public:
      TKDeclareParam(VariantCallback, Capture);
      TKDeclareParam(VariantCallback, CenterToEnvironment);

     private:
      static constexpr float CaptureDebounceTime = 500.0f; //!< Debounce duration in milliseconds.
      float m_captureTimerStart                  = 0.0f;   //!< Timestamp when debounce timer was started.
      bool m_captureTimerActive                  = false;  //!< True while a debounce timer is running.
    };

    typedef std::shared_ptr<EditorEnvironmentComponent> EditorEnvironmentComponentPtr;

  } // namespace Editor
} // namespace ToolKit
