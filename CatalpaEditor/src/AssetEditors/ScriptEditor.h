#pragma once
#include "AssetEditors/IAssetEditor.h"

namespace Catalpa::Editor {
	/* 文本草稿可暂含语法错误；Validate/Cook 拒绝错误版本，运行中的旧版本继续有效。 */
	class ScriptEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& doc) const override {
			return doc.Source() &&
			    (doc.TypeKey() == "script" || doc.TypeKey() == "prefab" || doc.TypeKey() == "material_definition");
		}
		/**
		 * @brief 从当前文档读取 Lua 脚本数据，绘制对应的属性和操作控件。
		 * @details 第 1 个参数（Application&）为输入。提供宿主服务的应用实例。
		 * @details 第 3 个参数（const PanelText&）为输入。按语言键、回退文本和稳定标识生成界面标签的回调。
		 *
		 * @param[in] doc 当前激活的资源文档。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string&) override;
		/**
		 * @brief 结束 Lua 脚本文档的编辑会话并释放编辑器持有的临时状态。
		 * @details 第 1 个参数（Application&）为输入。提供宿主服务的应用实例。
		 */
		void Close(Application&) override {
			m_Source.reset();
			m_Buffer.clear();
			m_Group = 0;
		}

	private:
		std::shared_ptr<const std::string> m_Source;
		std::vector<char> m_Buffer;
		uint64_t m_Group = 0;
	};
}
