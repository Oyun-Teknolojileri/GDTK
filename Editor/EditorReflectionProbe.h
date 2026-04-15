#pragma once

#include "EditorTypes.h"

#include <ReflectionProbe.h>
#include <ToolKit.h>
#include <Types.h>

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API EditorReflectionProbe : public ReflectionProbe
    {
     public:
      TKDeclareClass(EditorReflectionProbe, ReflectionProbe);

      EditorReflectionProbe();
      virtual ~EditorReflectionProbe();

      ObjectPtr Copy() const override;

      /** Starts or restarts a debounce timer. When the timer expires, CaptureEnvironment is triggered. */
      void FireCaptureInvalidate();

     protected:
      void ParameterConstructor() override;
      void ParameterEventConstructor() override;
      XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
      XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

     private:
      void ScheduleCapturePollingTask();

     public:
      TKDeclareParam(VariantCallback, Capture);
      TKDeclareParam(VariantCallback, CenterToVolume);

     private:
      static constexpr float CaptureDebounceTime = 500.0f; //!< Debounce duration in milliseconds.
      float m_captureTimerStart                  = 0.0f;   //!< Timestamp when debounce timer was started.
      bool m_captureTimerActive                  = false;  //!< True while a debounce timer is running.
    };

    typedef std::shared_ptr<EditorReflectionProbe> EditorReflectionProbePtr;

  } // namespace Editor
} // namespace ToolKit
