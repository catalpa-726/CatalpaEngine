#pragma once

#include "Scene/SceneHandle.h"
#include "Runtime/GameplayModule.h"

#include <memory>

namespace Catalpa {
	class SceneManager;
	class AssetManager;

	/* 沙盒只提供示例内容/固定步回调和诊断；输入、时钟、调度与提取由引擎执行。 */
	class BoxGameplay {
	public:
		/**
		 * @brief 创建并激活沙盒样例场景，绑定相机、几何体与固定步更新。
		 * reversedZ只选择本样例的镜头契约；渲染后端根据提取的lens匹配深度状态。
		 *
		 * @param[in,out] scenes 创建并激活样例场景的管理器。
		 * @param[in,out] assets 查找样例网格、材质和着色器的资产管理器。
		 * @param[in] reversedZ 是否使用反向 Z 的深度约定。
		 * @param[in] errorMaterial 为真时给样例立方体绑定引用缺失着色器的材质，用于验证渲染回退。
		 *
		 * @return 样例场景、资源和系统全部安装成功时为 true，安装中断时为 false。
		 */
		bool Attach(SceneManager& scenes, AssetManager& assets, bool reversedZ = false, bool errorMaterial = false);
		/**
		 * @brief 在层级系统首次发布有效变换后，记录样例子实体的世界坐标。
		 * 层级系统首次发布有效的世界变换后，记录一次示例坐标供核对。
		 */
		void ReportHierarchyOnce();
		/**
		 * @brief 从管理器卸载示例游戏场景并清理关联状态。
		 */
		void Detach();

	private:
		struct State;
		static SystemDesc CameraSystem(std::shared_ptr<State>);
		SceneManager* m_Scenes = nullptr;
		SceneHandle m_Scene{};
		GameplayModule m_Module;
		std::shared_ptr<State> m_State;
		bool m_ReportedHierarchy = false;
	};
}
