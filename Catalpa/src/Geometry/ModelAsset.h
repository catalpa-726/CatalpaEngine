#pragma once
#include "Assets/AssetHandle.h"
#include "Core/Math/TransformMath.h"
#include "Geometry/MeshAsset.h"

namespace Catalpa {
    struct MaterialInstance;
    // 导入模型局部身份与父索引，独立于World EntityId；TRS遵循米/弧度、T*R*S约定。
    struct ModelNode {
        StableId id;
        std::string name;
        int32_t parent = -1;
        Math::TransformTRS local;
    };
    struct ModelMeshInstance {
        uint32_t node = 0;
        AssetHandle<MeshAsset> mesh;
        std::vector<AssetHandle<MaterialInstance>> materials;
    };
    // 一个模型可以包含多个节点和mesh实例；逻辑资产引用不等于已经加载其依赖。
    struct ModelAsset { std::vector<ModelNode> nodes; std::vector<ModelMeshInstance> meshes; };

    // 验证节点身份、父关系/环、TRS与逻辑引用，不创建World，也不重排或归一化作者数据。
    [[nodiscard]] inline bool ValidateModelAsset(const ModelAsset& model, std::string& error) {
        if (model.nodes.empty() || model.nodes.size() > 65536 || model.meshes.size() > 65536) {
            error = "Model node/mesh count is outside supported bounds"; return false;
        }
        try {
            std::set<StableId> identities;
            for (size_t i = 0; i < model.nodes.size(); ++i) {
                const auto& node = model.nodes[i];
                if (!node.id.HasValue() || !identities.insert(node.id).second || node.name.size() > 1024
                    || node.name.find('\0') != std::string::npos) { error = "Model node identity or name is invalid"; return false; }
                if (node.parent < -1 || node.parent >= static_cast<int32_t>(model.nodes.size()) || node.parent == static_cast<int32_t>(i)) {
                    error = "Model node parent is invalid"; return false;
                }
                const auto trs = Math::ValidateTRS(node.local);
                if (!trs.IsSuccess()) { error = trs.message; return false; }
            }
            std::vector<uint8_t> visited(model.nodes.size());
            for (size_t i = 0; i < model.nodes.size(); ++i) {
                int32_t cursor = static_cast<int32_t>(i);
                while (cursor >= 0 && visited[static_cast<size_t>(cursor)] == 0) {
                    visited[static_cast<size_t>(cursor)] = 1; cursor = model.nodes[static_cast<size_t>(cursor)].parent;
                }
                if (cursor >= 0 && visited[static_cast<size_t>(cursor)] == 1) { error = "Model hierarchy contains a cycle"; return false; }
                cursor = static_cast<int32_t>(i);
                while (cursor >= 0 && visited[static_cast<size_t>(cursor)] == 1) {
                    visited[static_cast<size_t>(cursor)] = 2; cursor = model.nodes[static_cast<size_t>(cursor)].parent;
                }
            }
            for (const auto& instance : model.meshes) {
                if (instance.node >= model.nodes.size() || !instance.mesh.HasValue() || instance.materials.empty()
                    || instance.materials.size() > 65536) { error = "Model mesh instance or materials are invalid"; return false; }
                for (const auto material : instance.materials) if (!material.HasValue()) { error = "Model material reference is invalid"; return false; }
            }
            error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    // 依赖解析后进一步核对每个mesh的完整材质槽；缺失依赖不以默认空mesh伪装成功。
    [[nodiscard]] inline bool ValidateModelMeshBindings(const ModelAsset& model,
        std::span<const AssetVersionRef<MeshAsset>> meshes, std::string& error) {
        if (!ValidateModelAsset(model, error)) return false;
        for (const auto& instance : model.meshes) {
            const auto found = std::find_if(meshes.begin(), meshes.end(), [&](const auto& value) {
                return value.HasValue() && value.GetId() == instance.mesh.id;
            });
            if (found == meshes.end()) { error = "Model mesh dependency is missing"; return false; }
            if (!ValidateMeshAsset(*found->Get(), error)) return false;
            if (instance.materials.size() != found->Get()->materialSlots.size()) {
                error = "Model material count does not match mesh slots"; return false;
            }
        }
        error.clear(); return true;
    }
}
