#pragma once
#include "UI/UIDocumentAsset.h"
#include "UI/UIDrawData.h"
#include "UI/UIEvent.h"

namespace Catalpa {
	/* 所有 RmlUi 操作位于创建线程；快照及事件均拥有自己的数据。 */
	class CATALPA_API UIRuntime final {
	public:
		UIRuntime();
		~UIRuntime();
		/**
		 * @brief 安装界面的原生接口或建立给定资产的 RmlUi 上下文。
		 *
		 * @param[in] asset 初始化界面文档所用的固定资产版本。
		 * @param[in] width UI 或渲染目标的宽度，以像素计。
		 * @param[in] height UI 或渲染目标的高度，以像素计。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Initialize(AssetVersionRef<UIDocumentAsset>, uint32_t width, uint32_t height, std::string& error);
		/**
		 * @brief 更新 RmlUi 上下文的布局尺寸。
		 *
		 * @param[in] width UI 或渲染目标的宽度，以像素计。
		 * @param[in] height UI 或渲染目标的高度，以像素计。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Resize(uint32_t width, uint32_t height, std::string& error);
		/**
		 * @brief 设置密度。
		 *
		 * @param[in] pixelsPerDp 每个密度无关像素所对应的设备像素数。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetDensity(float pixelsPerDp, std::string& error);
		/**
		 * @brief 按经过时间推进游戏界面的状态与布局。
		 *
		 * @param[in] elapsedSeconds 经过的时间。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Advance(double elapsedSeconds, std::string& error);
		/**
		 * @brief 转发游戏界面输入，并报告事件是否由界面消费。
		 *
		 * @param[in] input 输入的 UIInputEvent。
		 * @param[in] consumed 消费状态。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Input(const UIInputEvent&, bool& consumed, std::string& error);
		/**
		 * @brief 按绑定标识更新 UI 模型值、表单值或元素文本，并处理活跃预编辑。
		 * 外部写入表单前按 Unicode 标量数检查 maxlength；超限保留旧值，合法赋值先取消该控件的预编辑。
		 *
		 * @param[in] element 目标表单控件的文档内标识。
		 * @param[in] text 用来替换控件当前内容的 UTF-8 文本。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 目标找到且成功应用新值时为 true，输入非法或超出控件长度限制时为 false。
		 */
		bool SetValue(std::string_view element, std::string_view text, std::string& error);
		/**
		 * @brief 返回值。
		 *
		 * @param[in] element 文档内目标表单或界面元素的标识。
		 * @param[out] text 成功时接收目标表单控件当前保存的 UTF-8 文本。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool GetValue(std::string_view element, std::string& text, std::string& error) const;
		/**
		 * @brief 通过元素标识设置当前输入焦点。
		 *
		 * @param[in] element 文档内目标表单或界面元素的标识。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Focus(std::string_view element, std::string& error);
		/**
		 * @brief 采集 RmlUi 绘制结果并发布可跨上下文生命周期持有的帧快照。
		 * 发布由快照持有的绘制数据，使用方可在 RmlUi 上下文移除后继续持有该帧。
		 *
		 * @param[out] output 成功时接收独立持有顶点、索引和绘制命令的只读快照。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 渲染和快照发布成功时为 true，否则为 false。
		 */
		bool Render(std::shared_ptr<const UIDrawData>& output, std::string& error);
		/**
		 * @brief 检查是否具有焦点。
		 *
		 * @return 满足上述条件时为 true，否则为 false。
		 */
		bool HasFocus() const;
		TextInputSnapshot TextInput() const;
		/**
		 * @brief 取出事件。
		 *
		 * @return 本次操作收集的结果，元素类型为 UIEvent。
		 */
		std::vector<UIEvent> TakeEvents();
		/**
		 * @brief 取消组合输入并从 RmlUi 移除上下文。
		 */
		void Shutdown() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} /* 命名空间 Catalpa */
