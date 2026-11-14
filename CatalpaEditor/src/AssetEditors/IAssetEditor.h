#pragma once
#include "Documents/AssetDocument.h"
#include "Panels/PanelText.h"

namespace Catalpa {
	class Application;
}

namespace Catalpa::Editor {
	/* 类型编辑器只管理展示副本和离屏视图；撤销历史由资产文档拥有。 */
	class IAssetEditor {
	public:
		virtual ~IAssetEditor() = default;
		/**
		 * @brief 判断具体编辑器是否接受当前文档。
		 * @details 第 1 个参数（const AssetDocument&）为输入。当前资源编辑器针对的文档。
		 *
		 * @return 该编辑器支持此文档的资产类型时为 true。
		 */
		virtual bool Accepts(const AssetDocument&) const = 0;
		/**
		 * @brief 绘制具体资源编辑器的当前文档。
		 * @details 第 1 个参数（Application&）为输入。提供宿主服务的应用实例。
		 * @details 第 2 个参数（AssetDocument&）为输入输出。当前资源编辑器针对的文档。
		 * @details 第 3 个参数（const PanelText&）为输入。按语言键、回退文本和稳定标识生成界面标签的回调。
		 *
		 * @param[out] error 失败时接收原因说明。
		 */
		virtual void Draw(Application&, AssetDocument&, const PanelText&, std::string& error) = 0;
		/**
		 * @brief 结束当前资源编辑会话。
		 * @details 第 1 个参数（Application&）为输入。提供宿主服务的应用实例。
		 */
		virtual void Close(Application&) = 0;
	};
}
