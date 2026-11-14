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
		/**
		 * @brief 校验并应用游戏界面的密度等运行配置。
		 *
		 * @param[in] config 用于创建界面运行时的配置。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Configure(UIRuntimeConfig, std::string& error);
		/**
		 * @brief 将场景中有效的 UI 组件与其运行上下文同步。
		 *
		 * @param[in] handle 目标对象的场景句柄。
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] width UI 或渲染目标的宽度，以像素计。
		 * @param[in] height UI 或渲染目标的高度，以像素计。
		 * @param[in] elapsed 经过的时间。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Update(SceneHandle, const Scene*, AssetManager&, uint32_t width, uint32_t height, double elapsed,
		    std::string& error);
		/**
		 * @brief 向适合的游戏界面转发事件，并传出是否已被消费。
		 *
		 * @param[in] event 路由给活动界面的输入事件。
		 * @param[in] consumed 消费状态。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Input(const UIInputEvent&, bool& consumed, std::string& error);
		/**
		 * @brief 设置值。
		 *
		 * @param[in] entity 操作所针对的实体。
		 * @param[in] element 文档内目标表单或界面元素的标识。
		 * @param[in] text 要写入表单控件的文本值。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetValue(PersistentEntityId, std::string_view element, std::string_view text, std::string& error);
		/**
		 * @brief 检查是否具有焦点。
		 *
		 * @return 满足上述条件时为 true，否则为 false。
		 */
		bool HasFocus() const;
		/**
		 * @brief 从界面顺序中查询当前文本编辑目标。
		 *
		 * @return 由后向前查找到的第一个活跃文本输入快照；没有活跃目标则返回默认快照。
		 */
		TextInputSnapshot TextInput() const;
		/**
		 * @brief 取出事件。
		 *
		 * @return 本次操作收集的结果，元素类型为 UIEvent。
		 */
		std::vector<UIEvent> TakeEvents();
		const std::shared_ptr<const UIDrawData>& DrawData() const noexcept;
		/**
		 * @brief 移除界面上下文并释放系统持有的资源。
		 */
		void Shutdown() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} /* 命名空间 Catalpa */
