#include "catpch.h"
#include "Geometry/PrimitiveMesh.h"
#include "Geometry/MeshValidation.h"

namespace Catalpa {
    namespace {
        // 显式 float 数组定义32字节CPU布局；没有把GLM对象sizeof当作GPU ABI。
        struct Vertex { float position[3]; float normal[3]; float uv[2]; };
        static_assert(sizeof(Vertex) == 32 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, uv) == 24);

        bool ValidHalfExtent(float size) noexcept
        {
            return std::isfinite(size) && size > 0.0f && size * 0.5f > 0.0f;
        }

        // 生成器使用同一布局/验证入口，不构造另一套临时GPU几何所有者。
        MeshStatus FinishMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices, const Aabb& bounds, MeshData& output)
        {
            try {
                MeshData candidate;
                candidate.vertexCount = static_cast<uint32_t>(vertices.size());
                candidate.vertexStride = sizeof(Vertex);
                candidate.attributes = {
                    { 0, VertexFormat::Float3, 0, VertexSemantic::Position },
                    { 1, VertexFormat::Float3, 12, VertexSemantic::Normal },
                    { 2, VertexFormat::Float2, 24, VertexSemantic::TexCoord0 }
                };
                candidate.vertexData.resize(vertices.size_bytes());
                std::memcpy(candidate.vertexData.data(), vertices.data(), vertices.size_bytes());
                candidate.indexData.resize(indices.size_bytes());
                std::memcpy(candidate.indexData.data(), indices.data(), indices.size_bytes());
                candidate.submeshes.push_back({ 0, static_cast<uint32_t>(indices.size()), 0, 0, candidate.vertexCount, 0, MeshTopology::TriangleList });
                candidate.localBounds = bounds;
                if (const auto status = ValidateMesh(candidate); !status.IsSuccess()) return status;
                output = std::move(candidate);
                return {};
            } catch (const std::bad_alloc&) { return { MeshError::AllocationFailed, "Cannot allocate primitive mesh" }; }
        }

        // 每面4顶点，u×v=normal；因此统一索引0,1,2 / 2,3,0从外侧看逆时针。
        void WriteFace(std::span<Vertex, 4> vertices, const Math::Vec3& normal, const Math::Vec3& u, const Math::Vec3& v, float half) noexcept
        {
            constexpr std::array<float, 4> U{ -1.0f, 1.0f, 1.0f, -1.0f };
            constexpr std::array<float, 4> V{ -1.0f, -1.0f, 1.0f, 1.0f };
            constexpr std::array<std::array<float, 2>, 4> UV{{ {0.0f,0.0f},{1.0f,0.0f},{1.0f,1.0f},{0.0f,1.0f} }};
            for (size_t i = 0; i < 4; ++i) {
                const Math::Vec3 position = (normal + u * U[i] + v * V[i]) * half;
                vertices[i] = { {position.x,position.y,position.z}, {normal.x,normal.y,normal.z}, {UV[i][0],UV[i][1]} };
            }
        }
    }

    MeshStatus CreateCubeMesh(float size, MeshData& output)
    {
        if (!ValidHalfExtent(size)) return { MeshError::InvalidArgument, "Cube size must be finite, positive and representable" };
        const float half = size * 0.5f;
        std::array<Vertex, 24> vertices;
        // 顺序为+X,-X,+Y,-Y,+Z,-Z；切向基按右手积明确排列。
        WriteFace(std::span<Vertex, 4>(vertices.data() + 0, 4), {1,0,0}, {0,0,-1}, {0,1,0}, half);
        WriteFace(std::span<Vertex, 4>(vertices.data() + 4, 4), {-1,0,0}, {0,0,1}, {0,1,0}, half);
        WriteFace(std::span<Vertex, 4>(vertices.data() + 8, 4), {0,1,0}, {1,0,0}, {0,0,-1}, half);
        WriteFace(std::span<Vertex, 4>(vertices.data() + 12, 4), {0,-1,0}, {1,0,0}, {0,0,1}, half);
        WriteFace(std::span<Vertex, 4>(vertices.data() + 16, 4), {0,0,1}, {1,0,0}, {0,1,0}, half);
        WriteFace(std::span<Vertex, 4>(vertices.data() + 20, 4), {0,0,-1}, {-1,0,0}, {0,1,0}, half);
        std::array<uint32_t, 36> indices;
        constexpr std::array<uint32_t, 6> faceIndices{ 0,1,2,2,3,0 };
        for (uint32_t face = 0; face < 6; ++face)
            for (uint32_t index = 0; index < 6; ++index) indices[face * 6 + index] = face * 4 + faceIndices[index];
        return FinishMesh(vertices, indices, Aabb{ Math::Vec3{-half}, Math::Vec3{half}, false }, output);
    }

    MeshStatus CreatePlaneMesh(float width, float depth, MeshData& output)
    {
        if (!ValidHalfExtent(width) || !ValidHalfExtent(depth)) return { MeshError::InvalidArgument, "Plane width/depth must be finite, positive and representable" };
        const float x = width * 0.5f, z = depth * 0.5f;
        const std::array<Vertex, 4> vertices{{
            { {-x,0,z}, {0,1,0}, {0,0} }, { {x,0,z}, {0,1,0}, {1,0} },
            { {x,0,-z}, {0,1,0}, {1,1} }, { {-x,0,-z}, {0,1,0}, {0,1} }
        }};
        constexpr std::array<uint32_t, 6> indices{ 0,1,2,2,3,0 };
        return FinishMesh(vertices, indices, Aabb{ {-x,0,-z}, {x,0,z}, false }, output);
    }
}
