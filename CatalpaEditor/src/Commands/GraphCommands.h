#pragma once
#include "Documents/AssetDocument.h"

namespace Catalpa::Editor {
    // 图与文本编辑共享文档事务；语义校验由相应编辑器在提交前完成。
    bool ChangeAuthorSource(AssetDocument&, std::string next, std::string& error, uint64_t editGroup = 0);
}
