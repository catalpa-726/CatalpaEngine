#include "catpch.h"
#include "MeshLod.h"
namespace Catalpa {
    bool ValidateMeshLodChain(const AssetVersionRef<MeshAsset>& base,
                              std::span<const AssetVersionRef<MeshAsset>> levels,
                              std::span<const float> errors, std::string& error) {
        if (levels.size() != errors.size() || levels.size() > 8 || (!levels.empty() && !base.HasValue())) {
            error = "Invalid LOD count/base version";
            return false;
        }
        // 非空链将读取基础几何的槽位与 submesh；有版本引用并不代表其载荷已受检。
        // 空链保留既有 CPU 几何入口，由调用方校验实际 MeshResource。
        if (!levels.empty() && !ValidateMeshAsset(*base.Get(), error))
            return false;
        std::set<AssetId> ids;
        if (base.HasValue())
            ids.insert(base.GetId());
        float previous = 0;
        for (size_t index = 0; index < levels.size(); ++index) {
            const auto& level = levels[index];
            if (!level.HasValue() || !ids.insert(level.GetId()).second || !std::isfinite(errors[index]) ||
                errors[index] <= previous || !ValidateMeshAsset(*level.Get(), error)) {
                if (error.empty())
                    error = "Invalid LOD identity/error";
                return false;
            }
            const auto& mesh = *level.Get();
            const auto& original = *base.Get();
            if (mesh.materialSlots != original.materialSlots ||
                mesh.mesh->GetData().submeshes.size() != original.mesh->GetData().submeshes.size()) {
                error = "LOD changes material/submesh identity";
                return false;
            }
            for (size_t submesh = 0; submesh < mesh.mesh->GetData().submeshes.size(); ++submesh)
                if (mesh.mesh->GetData().submeshes[submesh].materialSlot !=
                    original.mesh->GetData().submeshes[submesh].materialSlot) {
                    error = "LOD changes material slot mapping";
                    return false;
                }
            previous = errors[index];
        }
        error.clear();
        return true;
    }
} // namespace Catalpa
