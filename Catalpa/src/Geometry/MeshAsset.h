#pragma once
#include "catpch.h"
#include "Geometry/MeshData.h"

namespace Catalpa {
    // 多实例共用受检CPU几何及稳定材质槽名；不保存实体Transform或GPU所有权。
    // LOD与skin binding仍由对应后续阶段扩展，当前cooked loader必须拒绝不支持的版本/字段。
    struct MeshAsset { MeshResourceRef mesh; std::vector<std::string> materialSlots; };

    // 核对实际mesh与submesh槽映射，不修复顶点/索引或静默补齐材质槽。
    [[nodiscard]] inline bool ValidateMeshAsset(const MeshAsset& asset, std::string& error) {
        const auto status = ValidateMeshResource(asset.mesh);
        if (!status.IsSuccess()) { error = status.message; return false; }
        if (asset.materialSlots.empty() || asset.materialSlots.size() > 65536) {
            error = "Mesh asset requires 1..65536 material slots"; return false;
        }
        try {
            std::set<std::string_view> names;
            for (const auto& name : asset.materialSlots) {
                if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos || !names.insert(name).second) {
                    error = "Mesh material slots require unique bounded names"; return false;
                }
            }
            for (const auto& submesh : asset.mesh->GetData().submeshes) {
                if (submesh.materialSlot >= asset.materialSlots.size()) { error = "Submesh material slot is out of range"; return false; }
            }
            error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
