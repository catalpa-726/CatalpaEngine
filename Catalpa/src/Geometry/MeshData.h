#pragma once

#include "catpch.h"
#include "Core/Base.h"
#include "Geometry/Bounds.h"
#include "Geometry/Submesh.h"
#include "Geometry/VertexLayout.h"

namespace Catalpa {

    enum class MeshIndexType { UInt16, UInt32 };

    [[nodiscard]] constexpr uint32_t GetMeshIndexByteSize(MeshIndexType type) noexcept
    {
        switch (type) { case MeshIndexType::UInt16: return 2; case MeshIndexType::UInt32: return 4; default: return 0; }
    }

    // 作者/生成器拥有的 CPU 几何，只支持单 interleaved float stream、triangle list。
    // skin 权重、LOD 关系和资产身份由独立领域对象关联；本结构不隐式修复或改写源数据。
    struct MeshData {
        uint32_t vertexCount = 0;
        uint32_t vertexStride = 0;
        std::vector<VertexAttribute> attributes;
        std::vector<std::byte> vertexData;
        MeshIndexType indexType = MeshIndexType::UInt32;
        std::vector<std::byte> indexData;
        std::vector<Submesh> submeshes;
        Aabb localBounds;

        // 返回的 attributes 只在该 MeshData 有效且未修改期间借用。
        [[nodiscard]] VertexLayout GetVertexLayout() const noexcept { return { vertexStride, attributes }; }
        [[nodiscard]] size_t GetIndexCount() const noexcept {
            const auto size = GetMeshIndexByteSize(indexType);
            return size != 0 ? indexData.size() / size : 0;
        }
    };

    enum class MeshError { None, InvalidArgument, InvalidLayout, InvalidIndex, InvalidSubmesh, InvalidBounds, NonFinite, OutOfRange, AllocationFailed };
    struct MeshStatus {
        MeshError code = MeshError::None;
        const char* message = "";
        [[nodiscard]] bool IsSuccess() const noexcept { return code == MeshError::None; }
    };

    class MeshResource;
    using MeshResourceRef = std::shared_ptr<const MeshResource>;
    [[nodiscard]] CATALPA_API MeshStatus CreateMeshResource(const MeshData& source, MeshResourceRef& output);
    [[nodiscard]] CATALPA_API MeshStatus ValidateMeshResource(const MeshResourceRef& resource) noexcept;

    // 唯一工厂在完整校验后深复制到真正 const 对象，调用者不能留存可写别名。
    // 这里只是内置 CPU 几何保活值，不注册资产、创建 GPU 缓冲或持有 Scene。
    class CATALPA_API MeshResource {
    public:
        [[nodiscard]] const MeshData& GetData() const noexcept { return m_Data; }
        ~MeshResource() = default;
        MeshResource(const MeshResource&) = delete;
        MeshResource& operator=(const MeshResource&) = delete;
    private:
        friend MeshStatus CreateMeshResource(const MeshData&, MeshResourceRef&);
        explicit MeshResource(const MeshData& source) : m_Data(source) {}
        const MeshData m_Data;
    };
}
