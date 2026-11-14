#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Animation/AnimationAssetLoader.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
#include "Scene/Components/AnimationPreviewComponent.h"
#include "Graphs/GraphCanvas.h"

namespace Catalpa::Editor {
	class AssetEditorRegistry;
	/* 草稿、历史和保存由 AssetDocument 拥有；编辑器只持有独立 Scene 和临时取样状态。 */
	class AnimationEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument&) const override;
		/**
		 * @brief 从当前文档读取骨架、动画片段、蒙皮绑定和混合树数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) override;
		/**
		 * @brief 结束骨架、动画片段、蒙皮绑定和混合树文档的编辑会话并释放编辑器持有的临时状态。
		 *
		 * @param[in,out] app 用于关闭动画预览表面并卸载临时场景的宿主。
		 */
		void Close(Application&) override;

	private:
		bool Preview(Application&, AssetDocument&, const PanelText&, std::string& error);
		void BlendCanvas(AssetDocument&, BlendTreeAsset&, const PanelText&, std::string& error);
		SceneHandle m_Scene;
		PersistentEntityId m_Actor, m_Mesh, m_Camera;
		StableId m_Document, m_Selected;
		GraphCanvas m_Canvas;
		std::shared_ptr<const std::string> m_Source;
		std::shared_ptr<const std::string> m_CachedSource;
		AnimationPreviewComponent m_CachedPreview;
		AssetVersionRef<MeshSkinBinding> m_CachedBinding;
		AssetVersionRef<SkeletonAsset> m_SkeletonSource;
		ValidatedClipEvaluator m_ClipEvaluator;
		BlendTreeRuntime m_BlendPreview;
		AssetId m_DraftIdentity;
		uint64_t m_DraftRevision = 0;
		std::string m_CachedBindingText, m_CachedClipText;
		std::vector<char> m_Buffer;
		std::array<char, 48> m_Binding{}, m_Clip{};
		double m_Time = 0, m_LastClock = 0;
		bool m_Playing = false;
		bool m_Frame = true;
		std::map<std::string, float, std::less<>> m_Parameters;
	};
	/**
	 * @brief 注册动画编辑器。
	 *
	 * @param[in] registry 接收骨架、动画片段、蒙皮绑定和混合树编辑器工厂的注册表。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool RegisterAnimationEditors(AssetEditorRegistry&, std::string& error);
} /* 命名空间 Catalpa::Editor */
