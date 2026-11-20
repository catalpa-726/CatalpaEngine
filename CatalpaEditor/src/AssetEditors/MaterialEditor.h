#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "AssetEditors/MaterialColorPicker.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
#include "Scene/Components/MeshRendererComponent.h"

namespace Catalpa {
	class AssetManager;
}

namespace Catalpa::Editor {
	/** @brief 将已经加载的图像绑定到材质草稿，保留颜色语义校验、预览版本和撤销历史。 */
	bool SetMaterialTexture(AssetDocument& doc, AssetManager& assets, const MaterialParameter& parameter,
	    AssetHandle<ImageAsset> image, std::string& error);

	class MaterialEditor final : public IAssetEditor {
	public:
		bool Accepts(const AssetDocument& d) const override { return bool(d.Material()); }

		/**
		 * @brief 从当前文档读取材质实例数据，绘制对应的属性和操作控件。
		 *
		 * @param[in] app 提供活动的引擎服务和编辑预览资源。
		 * @param[in] doc 当前激活的资源文档。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时写入诊断原因。
		 */
		void Draw(Application& app, AssetDocument& doc, const PanelText& text, std::string& error) override {
			Draw(app, doc, text, error, {});
		}
		/** @brief 使用当前工作区的图像选择器绘制材质编辑器。 */
		void Draw(Application&, AssetDocument&, const PanelText&, std::string& error, const MaterialTexturePicker&);
		/**
		 * @brief 绘制预览。
		 * @param[in] maxSize 该视图允许的最大逻辑边长。
		 *
		 * @param[in] app 提供场景、资产和渲染服务的应用对象。
		 * @param[in,out] doc 当前编辑的文档。
		 * @param[out] error 失败时接收原因说明。
		 */
		void DrawPreview(Application&, AssetDocument&, std::string& error, float maxSize = 512.f);
		/**
		 * @brief 结束材质实例文档的编辑会话并释放编辑器持有的临时状态。
		 *
		 * @param[in,out] app 用于关闭材质预览表面并卸载临时场景的宿主。
		 */
		void Close(Application&) override;

	private:
		MaterialColorPicker m_ColorPicker;
		/**
		 * @brief 切换预览几何体，并为无光照表面提供可关闭的形体线。
		 *
		 * @param[in,out] app 切换模型时负责释放上一预览场景的宿主。
		 * @param[in] doc 决定当前材质可用预览控件的草稿。
		 */
		void DrawPreviewControls(Application& app, const AssetDocument& doc);
		/* 滑动期间只替换预览实例，松开后将一个完整手势提交至文档历史。 */
		std::shared_ptr<const MaterialInstance> m_SliderSource, m_SliderPreview;
		StableId m_SliderDocument;
		uint32_t m_SliderId = 0;
		/**
		 * @brief 按参数声明的范围显示可拖动和键入的归一化数值，并将实时预览与一次性提交分开。
		 * @param[in] doc 提供基准实例和文档身份。
		 * @param[in] parameter 定义参数标识及物理范围内的附加限制。
		 * @param[in,out] value 输入当前文档值，完成手势时写入待提交的新值。
		 * @param[out] error 无法生成临时预览实例时接收原因。
		 * @return 手势结束且最终值发生变化时为 true。
		 */
		bool EditUnitScalar(AssetDocument& doc, const MaterialParameter& parameter, float& value, std::string& error);
		/** @brief 绘制当前材质定义允许的参数及渲染状态，并将修改提交至草稿历史。 */
		void DrawProperties(Application&, AssetDocument&, std::string& error, const MaterialTexturePicker&);
		bool UpdateScene(Application&, AssetDocument&, std::string&);
		/* 水平角、仰角以度计；相机沿自身 +Z 轴离开预览模型的原点。 */
		struct Orbit {
			float azimuth = 35, elevation = 20, distance = 2.5f;
		};
		Orbit m_Orbit;
		uint64_t m_ViewRevision = 1;
		StableId m_Document, m_MaterialKey;
		SceneHandle m_Scene;
		PersistentEntityId m_Object;
		MeshRendererComponent m_Renderer;
		std::shared_ptr<const MaterialInstance> m_Bound;
		uint64_t m_Version = 0;
		bool m_Sphere = true;
		bool m_ShowShapeGuides = true;
	};
}
