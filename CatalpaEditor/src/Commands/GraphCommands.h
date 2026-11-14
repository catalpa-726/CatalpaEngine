#pragma once
#include "Documents/AssetDocument.h"

namespace Catalpa::Editor {
	/**
	 * @brief 通过文档历史提交图或脚本的源文本变更。
	 * 图与文本编辑共享文档事务；语义校验由相应编辑器在提交前完成。
	 *
	 * @param[in,out] doc 维护作者源文本和撤销历史的资产文档。
	 * @param[in] next 要替换当前草稿的新源文本。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] editGroup 用于将连续文本编辑合并到同一撤销组的标识。
	 *
	 * @return 源文本已相同或编辑事务成功时为 true，文本或提交无效时为 false。
	 */
	bool ChangeAuthorSource(AssetDocument&, std::string next, std::string& error, uint64_t editGroup = 0);
}
