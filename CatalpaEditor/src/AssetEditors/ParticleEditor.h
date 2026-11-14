#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Particles/ParticleEffect.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
namespace Catalpa::Editor {
	class AssetEditorRegistry;
	class ParticleEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& document) const override { return document.TypeKey() == "particle_effect"; }
		/**
		 * @brief 从当前文档读取粒子效果数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] document 持有作者数据与撤销历史的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
		/**
		 * @brief 结束粒子效果文档的编辑会话并释放编辑器持有的临时状态。
		 *
		 * @param[in,out] app 用于关闭粒子预览并释放相关场景、资产的宿主。
		 */
		void Close(Application&) override;

	private:
		bool Preview(Application&, AssetDocument&, ParticleEffect, std::string& error);
		SceneHandle m_Scene;
		PersistentEntityId m_Emitter;
		StableId m_Document, m_Effect;
		std::shared_ptr<const std::string> m_Published;
		uint64_t m_Revision = 0, m_Restart = 1;
		bool m_Emitting = true;
	};
	bool RegisterParticleEditor(AssetEditorRegistry&, std::string& error);
} /* 命名空间 Catalpa::Editor */
