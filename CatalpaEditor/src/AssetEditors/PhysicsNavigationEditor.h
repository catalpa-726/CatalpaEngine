#pragma once
#include "AssetEditors/IAssetEditor.h"

namespace Catalpa::Editor {
	/* 领域作者设置与自有值预览；撤销/保存仍走AssetDocument。 */
	class PhysicsNavigationEditor final : public IAssetEditor {
	public:
		PhysicsNavigationEditor();
		/**
		 * @brief 结束 PhysicsNavigationEditor 对象的生命周期。
		 */
		~PhysicsNavigationEditor();
		/**
		 * @brief 检查文档的类型是否可由物理与导航资源编辑器打开。
		 *
		 * @param[in] doc 当前激活的资源文档。
		 *
		 * @return 类型键或作者文件后缀指向碰撞形状、物理材质或导航网格时为 true。
		 */
		bool Accepts(const AssetDocument&) const override;
		/**
		 * @brief 从当前文档读取物理与导航资源数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
		/**
		 * @brief 结束物理与导航资源文档的编辑会话并释放编辑器持有的临时状态。
		 * 接口传入的应用实例在此关闭操作中不使用。
		 * @details 第 1 个参数（Application&）为输入。提供宿主服务的应用实例。
		 */
		void Close(Application&) override;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} /* 命名空间 Catalpa::Editor */
