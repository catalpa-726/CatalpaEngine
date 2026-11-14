#pragma once
#include "catpch.h"

#include "Core/Base.h"
namespace Catalpa {
	class AssetManager;
	/**
	 * @brief 在资产管理器中注册内置资产并绑定内置材质所用的着色器版本。
	 * 各领域注册完成且 Unlit 着色器就绪后，绑定其确切版本并发布内置材质与单像素图像。
	 * 部分发布失败时丢弃本次启动用的管理器，避免在同一内容版本下替换已发布身份。
	 *
	 * @param[in,out] manager 已注册领域加载器的资产管理器，用于取得着色器并发布内置资源。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 所需内置资产全部准备并发布成功时为 true，否则为 false。
	 */
	CATALPA_API bool RegisterBuiltinAssets(AssetManager&, std::string& error);
}
