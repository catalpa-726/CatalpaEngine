#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Graphs/GraphCanvas.h"
#include "Scripting/BehaviorGraphAsset.h"

namespace Catalpa::Editor {
	class BehaviorGraphEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& doc) const override {
			return doc.TypeKey() == "behavior_graph" && bool(doc.Source());
		}
		/**
		 * @brief 从当前文档读取行为图数据，绘制对应的属性和操作控件。
		 * @details 第 3 个参数（const PanelText&）为输入。按语言键、回退文本和稳定标识生成界面标签的回调。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string&) override;
		void Close(Application&) override {
			m_Canvas.Reset();
			m_Selected = {};
		}

	private:
		void EditToolbar(AssetDocument&, BehaviorGraphAsset&, std::string& error);
		void EditCanvas(AssetDocument&, BehaviorGraphAsset&, std::string& error);
		void EditProperties(AssetDocument&, BehaviorGraphAsset&, std::string& error);
		void DrawDebug(Application&, const AssetDocument&, std::string& error);
		GraphCanvas m_Canvas;
		StableId m_Selected;
		std::array<char, 129> m_Variable{}, m_Event{};
	};
}
