#pragma once

#include "Core/Base.h"
#include "Layer.h"

#include <vector>

namespace Catalpa {

	class LayerStack
	{
	public:
		LayerStack() = default;
		~LayerStack();

		// 普通层插入在 Overlay 之前；挂载回调由 Application 调用。
		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);

		// 移除时调用 OnDetach，但不销毁对象；移除后由调用方管理。
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* overlay);

		// 按栈顺序或反向传播顺序遍历。
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

		// 逆序卸载并销毁仍在栈中的 Layer。
		void Clear();

	private:
		std::vector<Layer*> m_Layers;
		unsigned int m_LayerInsertIndex = 0;
	};

}