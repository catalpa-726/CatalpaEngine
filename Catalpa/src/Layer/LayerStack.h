#pragma once

#include "Core/Base.h"
#include "Layer.h"

#include <vector>

namespace Catalpa {

	class LayerStack {
	public:
		LayerStack() = default;
		~LayerStack();

		/**
		 * @brief 将普通层插入叠加层之前。
		 * 普通层插入在 Overlay 之前；挂载回调由 Application 调用。
		 *
		 * @param[in] layer 要插入覆盖层之前的普通层实例。
		 */
		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);

		/**
		 * @brief 从栈移除指定普通层，并调用其脱离回调。
		 * 移除时调用 OnDetach，但不销毁对象；移除后由调用方管理。
		 *
		 * @param[in] layer 从普通层区移除并通知解绑的实例，移除后由调用者管理。
		 */
		void PopLayer(Layer* layer);
		/**
		 * @brief 弹出覆盖层。
		 *
		 * @param[in] overlay 要从覆盖层区段移除的层对象。
		 */
		void PopOverlay(Layer* overlay);

		std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
		std::vector<Layer*>::iterator end() { return m_Layers.end(); }
		std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
		std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }

		std::vector<Layer*>::const_iterator begin() const { return m_Layers.begin(); }
		std::vector<Layer*>::const_iterator end() const { return m_Layers.end(); }
		std::vector<Layer*>::const_reverse_iterator rbegin() const { return m_Layers.rbegin(); }
		std::vector<Layer*>::const_reverse_iterator rend() const { return m_Layers.rend(); }

		LayerStack(const LayerStack&) = delete;
		LayerStack& operator=(const LayerStack&) = delete;

		/**
		 * @brief 从栈中取出所有层并调用卸载回调。
		 */
		void Clear();

	private:
		std::vector<Layer*> m_Layers;
		unsigned int m_LayerInsertIndex = 0;
	};

}