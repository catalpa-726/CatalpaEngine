#pragma once
#include "UI/UIRuntime.h"
#include "UI/UIRuntimeConfig.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
namespace Catalpa {
    class Scene;
    class AssetManager;
    class CATALPA_API UISystem final {
      public:
        UISystem();
        ~UISystem();
        bool Configure(UIRuntimeConfig, std::string& error);
        bool Update(SceneHandle, const Scene*, AssetManager&, uint32_t width, uint32_t height, double elapsed,
                    std::string& error);
        bool Input(const UIInputEvent&, bool& consumed, std::string& error);
        bool SetValue(PersistentEntityId, std::string_view element, std::string_view text,
                      std::string& error);
        bool HasFocus() const;
        TextInputSnapshot TextInput() const;
        std::vector<UIEvent> TakeEvents();
        const std::shared_ptr<const UIDrawData>& DrawData() const noexcept;
        void Shutdown() noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Catalpa
