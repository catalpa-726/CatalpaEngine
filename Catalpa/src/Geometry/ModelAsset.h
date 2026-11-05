#pragma once
#include "Assets/AssetHandle.h"
#include "Core/Math/TransformMath.h"
#include "Geometry/MeshAsset.h"

namespace Catalpa {
    struct MaterialInstance;
    // 导入模型局部身份与父索引，独立于World EntityId；T*R*S、角度弧度。
    // 局部源单位通过明确转换根/父scale合成，最终世界坐标为米，不伪改源顶点。
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
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }

    // 依赖解析后进一步核对每个mesh的完整材质槽；缺失依赖不以默认空mesh伪装成功。
    [[nodiscard]] inline bool ValidateModelMeshBindings(const ModelAsset& model,
                                                        std::span<const AssetVersionRef<MeshAsset>> meshes,
                                                        std::string& error) try {
        if (!ValidateModelAsset(model, error))
            return false;
        std::map<AssetId, const MeshAsset*> indexed;
        for (const auto& mesh : meshes) {
            if (!mesh.HasValue() || !indexed.emplace(mesh.GetId(), mesh.Get()).second) {
                error = "Missing or ambiguous mesh version: " + mesh.GetId().ToString();
                return false;
            }
            if (!ValidateMeshAsset(*mesh.Get(), error)) return false;
        }
        for (const auto& instance : model.meshes) {
            const auto found = indexed.find(instance.mesh.id);
            if (found == indexed.end()) {
                error = "Model mesh dependency is missing";
                return false;
            }
            if (instance.materials.size() != found->second->materialSlots.size()) {
                error = "Model material count does not match mesh slots";
                return false;
            }
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {

        error = exception.what();
        return false;
    }

    // 以米和列向量world=parent*local计算模型整体包围体；预览/验收共用，绝不缩放或改写模型。
    [[nodiscard]] inline bool ComputeModelBounds(const ModelAsset& model,
                                                 std::span<const AssetVersionRef<MeshAsset>> meshes,
                                                 Aabb& output, std::string& error) {
        try {
            if (!ValidateModelMeshBindings(model, meshes, error))
                return false;
            std::vector<Math::Mat4> worlds(model.nodes.size(), Math::Mat4{1});
            std::vector<std::vector<size_t>> children(model.nodes.size());
            std::vector<size_t> order;
            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (model.nodes[i].parent < 0)
                    order.push_back(i);
                else
                    children[size_t(model.nodes[i].parent)].push_back(i);
            }
            for (size_t i = 0; i < order.size(); ++i) {
                const auto index = order[i];
                Math::Mat4 local;
                const auto status = Math::ComposeTRS(model.nodes[index].local, local);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
                worlds[index] =
                    model.nodes[index].parent < 0 ? local : worlds[size_t(model.nodes[index].parent)] * local;
                order.insert(order.end(), children[index].begin(), children[index].end());
            }
            std::map<AssetId, const MeshAsset*> indexed;
            for (const auto& mesh : meshes)
                indexed.emplace(mesh.GetId(), mesh.Get());
            Aabb candidate;
            for (const auto& instance : model.meshes) {
                Aabb transformed;
                const auto status = TransformBounds(indexed.at(instance.mesh.id)->mesh->GetData().localBounds,
                                                    worlds[instance.node], transformed);
                if (!status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
                const auto merged = MergeBounds(candidate, transformed, candidate);
                if (!merged.IsSuccess()) {
                    error = merged.message;
                    return false;
                }
            }
            output = candidate;
            error.clear();
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }
}
