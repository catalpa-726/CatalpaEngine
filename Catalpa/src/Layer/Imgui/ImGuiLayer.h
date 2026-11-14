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

		/**
		 * @brief 建立 ImGui 上下文，配置字体、GLFW 输入与可用的平台窗口回调。
		 */
		virtual void OnAttach() override;
		/**
		 * @brief 销毁独立平台窗口，解除平台后端绑定并释放 ImGui 上下文。
		 */
		virtual void OnDetach() override;

		/**
		 * @brief 采集主窗口平台输入并开始新的 ImGui 帧。
		 * 主窗口的平台输入与 CPU 帧转换；GPU 对象由 Renderer 持有。
		 */
		void Begin();
		/**
		 * @brief 完成本帧 ImGui 绘制，复制主窗口绘制数据并收集平台窗口使用的纹理。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus End();
		bool IsReady() const { return m_Ready; }
		void SetPlatformRuntime(EngineRuntime& runtime) noexcept { m_PlatformRuntime = &runtime; }
		/**
		 * @brief 提交独立 ImGui 平台窗口的绘制与呈现，并返回平台状态。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus PresentPlatformWindows();
		/**
		 * @brief 返回供玩法输入路由使用的键盘和鼠标捕获状态。
		 *
		 * @return 界面已就绪时返回 ImGui 的键盘、鼠标捕获标志并启用玩法上下文；未就绪时捕获两者并禁用玩法。
		 */
		[[nodiscard]] InputCapture GetInputCapture() const;
		std::span<const std::byte> GetFontPixels() const { return m_FontPixels; }
		Extent2D GetFontExtent() const { return m_FontExtent; }
		const ImGuiFrameData& GetFrameData() const { return m_FrameData; }

		/**
		 * @brief 根据 ImGui 的键盘和鼠标捕获状态标记引擎输入事件。
		 *
		 * @param[in] event 可能被当前 UI 捕获的输入事件。
		 */
		void OnEvent(Event& event) override;

	private:
		static RHIStatus CopyFrame(const ImDrawData*, ImGuiFrameData&);
		void InstallPlatformRenderer();
		EngineRuntime* m_PlatformRuntime = nullptr;
		RHIStatus m_PlatformStatus{};
		ImGuiFrameData m_PlatformFrame;
		bool m_Ready = false;
		bool m_ContextCreated = false;
		/* ImGui 借用 GlyphRanges 指针；其存储须覆盖整个图集生命周期。 */
		ImVector<ImWchar> m_WorkspaceGlyphRanges;
		std::span<const std::byte> m_FontPixels;
		Extent2D m_FontExtent{};
		ImGuiFrameData m_FrameData;
	};

}
