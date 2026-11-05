#pragma once
#include "Application/EditorSelection.h"
#include "Properties/PropertyEditorRegistry.h"
#include "Panels/PanelText.h"

namespace Catalpa::Editor {
    class InspectorPanel {
      public:
        bool Draw(SceneDocument&, const EditorSelection&, const PanelText&, std::string& error);

      private:
        void DrawName(SceneDocument&, EntityReference, const IdentityComponent&, const PanelText&,
                      std::string&);
        std::vector<char> m_NameBuffer;
        EntityReference m_NameTarget;
        std::string m_NameAtActivation;
        PropertyEditorRegistry m_Properties;
    };
}
