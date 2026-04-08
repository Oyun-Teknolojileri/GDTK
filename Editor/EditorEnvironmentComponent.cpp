#include "EditorEnvironmentComponent.h"

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(EditorEnvironmentComponent, EnvironmentComponent);

    EditorEnvironmentComponent::EditorEnvironmentComponent() {}

    EditorEnvironmentComponent::~EditorEnvironmentComponent() {}

    ComponentPtr EditorEnvironmentComponent::Copy(EntityPtr ntt) { return Super::Copy(ntt); }

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
