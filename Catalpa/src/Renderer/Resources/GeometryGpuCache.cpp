#include "catpch.h"
#include "Renderer/Resources/GeometryGpuCache.h"
#include "Renderer/Uploads/UploadScheduler.h"
#include "RHI/RHIDevice.h"
namespace Catalpa {
    VertexLayout GeometryGpuResource::GetVertexLayout() noexcept
    {
        static constexpr std::array<VertexAttribute, 5> attributes{{
            { 0, VertexFormat::Float3, 0, VertexSemantic::Position },
            { 1, VertexFormat::Float3, 12, VertexSemantic::Normal },
            { 2, VertexFormat::Float4, 24, VertexSemantic::Tangent },
            { 3, VertexFormat::Float2, 40, VertexSemantic::TexCoord0 },
            { 4, VertexFormat::Float4, 48, VertexSemantic::Color0 }
        }};
        return { 64, attributes };
    }

    GeometryGpuResource::~GeometryGpuResource() {
        if (device) { device->ReleaseBuffer(indices); device->ReleaseBuffer(vertices); }
    }
    RHIStatus GeometryGpuCache::Acquire(const AssetVersionRef<MeshAsset>& asset, std::shared_ptr<GeometryGpuResource>& output)
    {
        constexpr const char* op = "GeometryGpuCache::Acquire";
        std::string error;
        if (!asset.HasValue())
            return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        const auto key = std::make_pair(asset.GetId(), asset.GetVersion());
        auto found = m_Entries.find(key);
        if (found != m_Entries.end() && found->second->asset.Get() != asset.Get())
            return { .code = RHIErrorCode::InvalidState, .operation = "GeometryGpuCache/identity-version collision" };
        if (found != m_Entries.end() && found->second->vertices.HasValue() && found->second->indices.HasValue()) {
            output = found->second;
            return {};
        }
        if (!ValidateMeshAsset(*asset.Get(), error)) return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        if (found == m_Entries.end()) {
            auto resource = std::make_shared<GeometryGpuResource>();
            resource->device = &m_Device; resource->asset = asset;
            // 先保存owner，再排队。第二次upload失败时第一项仍有明确所有者，重试不重复上传。
            found = m_Entries.emplace(key, std::move(resource)).first;
        }
        auto& resource = *found->second;
        const auto& mesh = asset.Get()->mesh->GetData();
        if (mesh.vertexData.empty() || mesh.indexData.empty()) return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        if (!resource.vertices.HasValue()) {
            // GPU派生流统一location，作者顶点流保持原样。无UV/切线的限制由材质绑定入口检查。
            std::vector<std::byte> vertices(size_t(mesh.vertexCount) * 64, std::byte{});
            resource.maximumVertexColor = 1.0f;
            resource.hasSourceNormal = false;
            for (uint32_t index = 0; index < mesh.vertexCount; ++index) {
                auto* destination = vertices.data() + size_t(index) * 64;
                const std::array<float, 4> defaultTangent{ 1, 0, 0, 1 };
                std::memcpy(destination + 24, defaultTangent.data(), sizeof(defaultTangent));
                const std::array<float, 4> defaultColor{ 1, 1, 1, 1 };
                std::memcpy(destination + 48, defaultColor.data(), sizeof(defaultColor));
                for (const auto& attribute : mesh.attributes) {
                    uint32_t offset = 0, bytes = 0;
                    switch (attribute.semantic) {
                    case VertexSemantic::Position: offset = 0; bytes = 12; break;
                    case VertexSemantic::Normal: offset = 12; bytes = 12; resource.hasSourceNormal = true; break;
                    case VertexSemantic::Tangent: offset = 24; bytes = 16; break;
                    case VertexSemantic::TexCoord0: offset = 40; bytes = 8; break;
                    case VertexSemantic::Color0: offset = 48; bytes = 16; break;
                    default: continue;
                    }
                    if (GetVertexFormatByteSize(attribute.format) != bytes)
                        return { .code = RHIErrorCode::InvalidDescription, .operation = "GeometryGpuCache/canonical attribute" };
                    std::memcpy(destination + offset, mesh.vertexData.data() + size_t(index) * mesh.vertexStride + attribute.offset, bytes);
                }
                std::array<float, 4> color;
                std::memcpy(color.data(), destination + 48, sizeof(color));
                for (size_t channel = 0; channel < 4; ++channel)
                    if (!std::isfinite(color[channel]) || color[channel] < 0 || color[channel] > (channel == 3 ? 1.0f : 1.0e8f))
                        return { .code = RHIErrorCode::InvalidDescription, .operation = "GeometryGpuCache/source color exceeds the explicit surface domain" };
                resource.maximumVertexColor = std::max({ resource.maximumVertexColor, color[0], color[1], color[2] });
                std::array<float, 2> uv;
                std::memcpy(uv.data(), destination + 40, sizeof(uv));
                if (!std::isfinite(uv[0]) || !std::isfinite(uv[1]) || std::abs(uv[0]) > 1.0e8f || std::abs(uv[1]) > 1.0e8f)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = "GeometryGpuCache/source UV exceeds the explicit 1e8 shading domain" };
            }
            if (const auto status = m_Uploads.CreateBuffer(vertices, RHIBufferUsage::Vertex, resource.vertices); !status.IsSuccess()) return status;
        }
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
