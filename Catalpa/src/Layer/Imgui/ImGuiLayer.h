#pragma once

#include "catpch.h"

#include "Layer/Layer.h"
#include "Renderer/UI/ImGuiFrameData.h"
#include "RHI/RHIError.h"
#include "RHI/RHITypes.h"
#include "Runtime/Input/InputActions.h"
#include "imgui.h"

namespace Catalpa {
    class EngineRuntime;

	class CATALPA_API ImGuiLayer : public Layer {
	public:
		ImGuiLayer();
		~ImGuiLayer();

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		// 主窗口的平台输入与 CPU 帧转换；GPU 对象由 Renderer 持有。
		void Begin();
		[[nodiscard]] RHIStatus End();
		bool IsReady() const { return m_Ready; }
        void SetPlatformRuntime(EngineRuntime& runtime) noexcept { m_PlatformRuntime = &runtime; }
        RHIStatus PresentPlatformWindows();
        [[nodiscard]] InputCapture GetInputCapture() const;
		std::span<const std::byte> GetFontPixels() const { return m_FontPixels; }
		Extent2D GetFontExtent() const { return m_FontExtent; }
		const ImGuiFrameData& GetFrameData() const { return m_FrameData; }

		void OnEvent(Event& event) override;

	private:
          static RHIStatus CopyFrame(const ImDrawData*, ImGuiFrameData&);
          void InstallPlatformRenderer();
          EngineRuntime* m_PlatformRuntime = nullptr;
          RHIStatus m_PlatformStatus{};
          ImGuiFrameData m_PlatformFrame;
          bool m_Ready = false;
          bool m_ContextCreated = false;
          // ImGui 借用 GlyphRanges 指针；其存储须覆盖整个图集生命周期。
          ImVector<ImWchar> m_WorkspaceGlyphRanges;
          std::span<const std::byte> m_FontPixels;
          Extent2D m_FontExtent{};
          ImGuiFrameData m_FrameData;
	};

}
