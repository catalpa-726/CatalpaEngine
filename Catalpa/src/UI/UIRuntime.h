#pragma once
#include "UI/UIDocumentAsset.h"
#include "UI/UIDrawData.h"
#include "UI/UIEvent.h"

namespace Catalpa {
    // 所有 RmlUi 操作位于创建线程；快照及事件均拥有自己的数据。
    class CATALPA_API UIRuntime final {
      public:
        UIRuntime();
        ~UIRuntime();
        bool Initialize(AssetVersionRef<UIDocumentAsset>, uint32_t width, uint32_t height,
                        std::string& error);
        bool Resize(uint32_t width, uint32_t height, std::string& error);
        bool SetDensity(float pixelsPerDp, std::string& error);
        bool Advance(double elapsedSeconds, std::string& error);
        bool Input(const UIInputEvent&, bool& consumed, std::string& error);
        bool SetValue(std::string_view element, std::string_view text, std::string& error);
        bool GetValue(std::string_view element, std::string& text, std::string& error) const;
        bool Focus(std::string_view element, std::string& error);
        bool Render(std::shared_ptr<const UIDrawData>& output, std::string& error);
        bool HasFocus() const;
        TextInputSnapshot TextInput() const;
        std::vector<UIEvent> TakeEvents();
        void Shutdown() noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Catalpa
