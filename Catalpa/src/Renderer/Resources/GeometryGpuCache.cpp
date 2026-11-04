#include "catpch.h"
#include "Renderer/Resources/GeometryGpuCache.h"
#include "Renderer/Uploads/UploadScheduler.h"
#include "RHI/RHIDevice.h"
namespace Catalpa {
    GeometryGpuResource::~GeometryGpuResource() {
        if (device) { device->ReleaseBuffer(indices); device->ReleaseBuffer(vertices); }
    }
    RHIStatus GeometryGpuCache::Acquire(const AssetVersionRef<MeshAsset>& asset, std::shared_ptr<GeometryGpuResource>& output)
    {
        constexpr const char* op = "GeometryGpuCache::Acquire";
        std::string error;
        if (!asset.HasValue() || !ValidateMeshAsset(*asset.Get(), error))
            return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        const auto key = std::make_pair(asset.GetId(), asset.GetVersion());
        auto found = m_Entries.find(key);
        if (found != m_Entries.end() && found->second->asset.Get() != asset.Get())
            return { .code = RHIErrorCode::InvalidState, .operation = "GeometryGpuCache/identity-version collision" };
        if (found == m_Entries.end()) {
            auto resource = std::make_shared<GeometryGpuResource>();
            resource->device = &m_Device; resource->asset = asset;
            // 先保存owner，再排队。第二次upload失败时第一项仍有明确所有者，重试不重复上传。
            found = m_Entries.emplace(key, std::move(resource)).first;
        }
        auto& resource = *found->second;
        const auto& mesh = asset.Get()->mesh->GetData();
        if (mesh.vertexData.empty() || mesh.indexData.empty()) return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        if (!resource.vertices.HasValue())
            if (const auto status = m_Uploads.CreateBuffer(mesh.vertexData, RHIBufferUsage::Vertex, resource.vertices); !status.IsSuccess()) return status;
        if (!resource.indices.HasValue())
            if (const auto status = m_Uploads.CreateBuffer(mesh.indexData, RHIBufferUsage::Index, resource.indices); !status.IsSuccess()) return status;
        output = found->second; return {};
    }
    void GeometryGpuCache::OnSubmitted() noexcept { for (auto& [key, value] : m_Entries) value->uploadPending = false; }
    void GeometryGpuCache::Prune() {
        for (auto it = m_Entries.begin(); it != m_Entries.end();)
            if (!it->second->uploadPending && it->second.use_count() == 1) it = m_Entries.erase(it); else ++it;
    }
}
