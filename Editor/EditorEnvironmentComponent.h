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

     protected:
      XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
      XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

     private:
      void ParameterConstructor() override;
    };

    typedef std::shared_ptr<EditorEnvironmentComponent> EditorEnvironmentComponentPtr;

  } // namespace Editor
} // namespace ToolKit
