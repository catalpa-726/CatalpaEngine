#pragma once
#include "Core/Id/StableId.h"

namespace Catalpa::Editor {
	/**
	 * @brief 从所有者标识和角色生成稳定的画布元素标识。
	 *
	 * @param[in] owner 标识持有该对象的上下文。
	 * @param[in] role 区分同一所有者下不同画布元素的角色键。
	 *
	 * @return 取所有者标识和角色组合文本的 SHA-256 前 16 字节所得的画布标识。
	 */
	StableId CanvasIdentity(StableId owner, std::string_view role);
	/**
	 * @brief 把图端口或节点的协议术语转换为当前语言的显示文本。
	 * 界面按当前语言显示模式术语；作者数据和画布标识继续使用注册时的拼写。
	 *
	 * @param[in] symbol 图模式中端口或节点的原始符号，用于查找界面译名。
	 *
	 * @return 当前语言的图术语，找不到翻译时使用后备文本。
	 */
	std::string GraphTerm(std::string_view symbol);
	struct CanvasPin {
		StableId id, node;
		std::string label;
		bool input = false;
	};
	struct CanvasNode {
		StableId id;
		std::string title;
		std::vector<CanvasPin> pins;
		float x = 0, y = 0;
	};
	struct CanvasLink {
		StableId id, from, to;
	};
	struct CanvasChanges {
		std::optional<std::pair<CanvasPin, CanvasPin>> connection;
		std::vector<StableId> removeNodes, removeLinks;
		std::map<StableId, std::pair<float, float>> positions;
		StableId selected;
	};
	class GraphCanvas {
	public:
		GraphCanvas() = default;
		~GraphCanvas();
		GraphCanvas(const GraphCanvas&) = delete;
		GraphCanvas& operator=(const GraphCanvas&) = delete;
		using ConnectionCheck = std::function<bool(const CanvasPin&, const CanvasPin&, std::string&)>;
		/**
		 * @brief 绘制节点和连线，并收集用户请求的图修改。
		 *
		 * @param[in] document 用于隔离各文档画布上下文的稳定标识。
		 * @param[in] nodes 参与处理的节点集合。
		 * @param[in] links 该文档当前已有的端口连接。
		 * @param[in] check 接受或拒绝候选端口连接并提供原因的回调。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 本帧请求的连接、节点与连线删除、节点位置和选中标识，供调用方提交到图文档。
		 */
		CanvasChanges Draw(StableId document, std::span<const CanvasNode>, std::span<const CanvasLink>,
		    const ConnectionCheck&, std::string& error);
		/**
		 * @brief 销毁节点编辑器上下文并清除画布状态。
		 */
		void Reset();
		void Focus(StableId node) noexcept { m_Focus = node; }

	private:
		uintptr_t Local(StableId);
		StableId Stable(uintptr_t) const;
		void* m_Context = nullptr;
		StableId m_Document;
		StableId m_Focus;
		bool m_CancelDrag = false;
		std::map<StableId, uintptr_t> m_Local;
		std::map<uintptr_t, StableId> m_Stable;
		uintptr_t m_Next = 1;
	};
}
