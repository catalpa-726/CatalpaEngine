#pragma once
#include "Documents/AssetDocument.h"
#include "Assets/AssetManager.h"

namespace Catalpa::Editor {
	/**
	 * @brief 校验材质参数及渲染状态，并以可撤销的文档事务提交。
	 * 参数与渲染状态先由领域层校验；编辑器只交换经验证的不可变草稿。
	 *
	 * @param[in,out] doc 接收新材质实例草稿并记录修改历史的资产文档。
	 * @param[in] parameters 要写入草稿的材质参数覆盖项。
	 * @param[in] states 要应用的材质渲染状态覆盖项。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] loadedAssets 可选资产管理器，用于按资产版本解析参数引用。
	 *
	 * @return 材质变更通过校验并由文档历史接受时为 true，校验或提交失败时为 false。
	 */
	bool ChangeMaterial(AssetDocument&, std::span<const MaterialParameterOverride>, const MaterialStateOverrides&,
	    std::string& error, const AssetManager* loadedAssets = nullptr);
	/**
	 * @brief 修改导入设置。
	 *
	 * @param[in,out] doc 当前编辑的文档。
	 * @param[in] unitScale 源坐标到引擎单位的缩放系数。
	 * @param[in] normals 源网格缺少法线时是否生成面法线。
	 * @param[in] tangents 源网格缺少切线时是否生成 MikkTSpace 切线。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	bool ChangeImportSettings(AssetDocument&, double unitScale, bool normals, bool tangents, std::string&);
}
