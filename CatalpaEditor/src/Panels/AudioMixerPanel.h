#pragma once
#include "Panels/PanelText.h"
namespace Catalpa {
    class Application;
}
namespace Catalpa::Editor {
    class AudioMixerPanel final {
      public:
        void Draw(Application&, const PanelText&, std::string& error);

      private:
        float m_Master = 1;
    };
} // namespace Catalpa::Editor
