/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
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

      // Key diamonds for animatable parameters.
      //////////////////////////////////////////

      /**
       * Key button drawn in front of an animatable parameter row. It shows what the clip of the dope
       * sheet holds for the parameter and keys / unkeys it: empty while the parameter has no track,
       * filled when the track has keys, outlined when a key sits on the playhead frame. Clicking sets
       * a key, right clicking opens the menu.
       *
       * Draws nothing when no owner prefix is pushed or the sheet holds no clip, so a row that can not
       * be addressed stays clean.
       */
      static void ShowKeyDiamond(ParameterVariant* var);

      /**
       * Sets the owner a row's track id is built from: "<entityName>", "<entityName>.<component>" or
       * "<entityName>.MaterialComponent.<index>". Every panel pushes the prefix of the block of rows
       * it draws and the diamond appends the parameter name.
       */
      static void PushKeyOwner(const String& owner);
      static void PopKeyOwner();

      /**
       * If multiple entities are selected, the variant with the name will be updated in all of the selection.
       * If a component class is provided, all selected entities' given component will be the target to update variant
       * for.
       */
      static ValueUpdateFn MultiUpdate(ParameterVariant* var, ClassMeta* componentClass = nullptr);
    };

  } // namespace Editor
} // namespace ToolKit