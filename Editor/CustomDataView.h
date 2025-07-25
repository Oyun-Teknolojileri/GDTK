/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "View.h"

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API CustomDataView : public View
    {
     public:
      CustomDataView();
      virtual ~CustomDataView();
      virtual void Show();

      static void ShowMaterialPtr(const String& uniqueName, const String& file, MaterialPtr& var, bool isEditable);
      static void ShowMaterialVariant(const String& uniqueName, const String& file, ParameterVariant* var);

      /**
       * Shows the parameters with given indexes.
       * @param entity is the subject which will show its data block.
       * @param headerName is the Name / Category that will appear in object inspector.
       * @param vars is the index list that points to data block that identifies the parameters to show.
       * @param isListEditable allows altering the shown variants.
       */
      static void ShowCustomData(EntityPtr entity, const String& headerName, const IntArray& vars, bool isListEditable);

      static bool BeginShowVariants(StringView header);

      /**
       * Shows the variant, sets remove to true if user choose to delete it.
       */
      static void ShowVariant(ParameterVariant* var, bool& remove, int uiId, bool isEditable);
      static void EndShowVariants();

      static void ShowVariant(ParameterVariant* var, ComponentPtr comp, ValueUpdateFn callback = nullptr);

      /**
       * If multiple entities are selected, the variant with the name will be updated in all of the selection.
       * If a component class is provided, all selected entities' given component will be the target to update variant
       * for.
       */
      static ValueUpdateFn MultiUpdate(ParameterVariant* var, ClassMeta* componentClass = nullptr);
    };

  } // namespace Editor
} // namespace ToolKit