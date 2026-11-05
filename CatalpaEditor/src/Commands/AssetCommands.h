#pragma once
#include "Documents/AssetDocument.h"
#include "Assets/AssetManager.h"

namespace Catalpa::Editor {
    // 参数与渲染状态先由领域层校验；编辑器只交换经验证的不可变草稿。
    bool ChangeMaterial(AssetDocument&, std::span<const MaterialParameterOverride>,
                        const MaterialStateOverrides&, std::string& error,
                        const AssetManager* loadedAssets = nullptr);
    bool ChangeImportSettings(AssetDocument&, double unitScale, bool normals, bool tangents, std::string&);
}
