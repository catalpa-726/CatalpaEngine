#include "catpch.h"

#include "Geometry/PrimitiveMesh.h"

#include "Geometry/MeshValidation.h"
#include "Core/Math/CoordinateConvention.h"

namespace Catalpa {
	namespace {
		/* 显式 float 数组定义32字节CPU布局；没有把GLM对象sizeof当作GPU ABI。 */
		struct Vertex {
			float position[3];
			float normal[3];
			float uv[2];
		};
		static_assert(sizeof(Vertex) == 32 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, uv) == 24);

		bool ValidHalfExtent(float size) noexcept {
			return std::isfinite(size) && size > 0.0f && size * 0.5f > 0.0f;
		}

		/**
		 * @brief 按统一顶点布局构造并校验基本几何网格。
		 * 生成器使用同一布局/验证入口，不构造另一套临时GPU几何所有者。
		 *
		 * @param[in] vertices 按位置、法线和纹理坐标排列的源顶点。
		 * @param[in] indices 引用源顶点的三角形索引序列。
		 * @param[in] bounds 与这些顶点对应的局部包围盒。
		 * @param[out] output 成功时接收封装并校验后的网格。
		 *
		 * @return 网格有效并已交付输出时返回成功，构造或校验失败时返回具体错误。
		 */
		MeshStatus FinishMesh(
		    std::span<const Vertex> vertices, std::span<const uint32_t> indices, const Aabb& bounds, MeshData& output) {
			try {
				MeshData candidate;
				candidate.vertexCount = static_cast<uint32_t>(vertices.size());
				candidate.vertexStride = sizeof(Vertex);
				candidate.attributes = {{0, VertexFormat::Float3, 0, VertexSemantic::Position},
				    {1, VertexFormat::Float3, 12, VertexSemantic::Normal},
				    {2, VertexFormat::Float2, 24, VertexSemantic::TexCoord0}};
				candidate.vertexData.resize(vertices.size_bytes());
				std::memcpy(candidate.vertexData.data(), vertices.data(), vertices.size_bytes());
				candidate.indexData.resize(indices.size_bytes());
				std::memcpy(candidate.indexData.data(), indices.data(), indices.size_bytes());
				candidate.submeshes.push_back({0, static_cast<uint32_t>(indices.size()), 0, 0, candidate.vertexCount, 0,
				    MeshTopology::TriangleList});
				candidate.localBounds = bounds;
				if (const auto status = ValidateMesh(candidate); !status.IsSuccess())
					return status;
				output = std::move(candidate);
				return {};
			} catch (const std::bad_alloc&) {
				return {MeshError::AllocationFailed, "Cannot allocate primitive mesh"};
			}
		}

		/**
		 * @brief 按面内切向基写出立方体一个面的四个顶点、法线和纹理坐标。
		 * 每面4顶点，u×v=normal；因此统一索引0,1,2 / 2,3,0从外侧看逆时针。
		 *
		 * @param[out] vertices 接收该面四个顶点的位置、法线和纹理坐标。
		 * @param[in] normal 立方体该面朝外的单位法线。
		 * @param[in] u 面内 U 轴的方向向量。
		 * @param[in] v 面内 V 轴的方向向量。
		 * @param[in] half 立方体边长的一半，用于确定角点位置。
		 */
		void WriteFace(std::span<Vertex, 4> vertices, const Math::Vec3& normal, const Math::Vec3& u,
		    const Math::Vec3& v, float half) noexcept {
			constexpr std::array<float, 4> U{-1.0f, 1.0f, 1.0f, -1.0f};
			constexpr std::array<float, 4> V{-1.0f, -1.0f, 1.0f, 1.0f};
			constexpr std::array<std::array<float, 2>, 4> UV{{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
			for (size_t i = 0; i < 4; ++i) {
				const Math::Vec3 position = (normal + u * U[i] + v * V[i]) * half;
				vertices[i] = {
				    {position.x, position.y, position.z}, {normal.x, normal.y, normal.z}, {UV[i][0], UV[i][1]}};
			}
		}
	}

	MeshStatus CreateCubeMesh(float size, MeshData& output) {
		if (!ValidHalfExtent(size))
			return {MeshError::InvalidArgument, "Cube size must be finite, positive and representable"};
		const float half = size * 0.5f;
		std::array<Vertex, 24> vertices;
		/* 顺序为+X,-X,+Y,-Y,+Z,-Z；切向基按右手积明确排列。 */
		WriteFace(std::span<Vertex, 4>(vertices.data() + 0, 4), {1, 0, 0}, {0, 0, -1}, {0, 1, 0}, half);
		WriteFace(std::span<Vertex, 4>(vertices.data() + 4, 4), {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, half);
		WriteFace(std::span<Vertex, 4>(vertices.data() + 8, 4), {0, 1, 0}, {1, 0, 0}, {0, 0, -1}, half);
		WriteFace(std::span<Vertex, 4>(vertices.data() + 12, 4), {0, -1, 0}, {1, 0, 0}, {0, 0, 1}, half);
		WriteFace(std::span<Vertex, 4>(vertices.data() + 16, 4), {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, half);
		WriteFace(std::span<Vertex, 4>(vertices.data() + 20, 4), {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, half);
		std::array<uint32_t, 36> indices;
		constexpr std::array<uint32_t, 6> faceIndices{0, 1, 2, 2, 3, 0};
		for (uint32_t face = 0; face < 6; ++face)
			for (uint32_t index = 0; index < 6; ++index)
				indices[face * 6 + index] = face * 4 + faceIndices[index];
		return FinishMesh(vertices, indices, Aabb{Math::Vec3{-half}, Math::Vec3{half}, false}, output);
	}

	MeshStatus CreatePlaneMesh(float width, float depth, MeshData& output) {
		if (!ValidHalfExtent(width) || !ValidHalfExtent(depth))
			return {MeshError::InvalidArgument, "Plane width/depth must be finite, positive and representable"};
		const float x = width * 0.5f, z = depth * 0.5f;
		const std::array<Vertex, 4> vertices{{{{-x, 0, z}, {0, 1, 0}, {0, 0}}, {{x, 0, z}, {0, 1, 0}, {1, 0}},
		    {{x, 0, -z}, {0, 1, 0}, {1, 1}}, {{-x, 0, -z}, {0, 1, 0}, {0, 1}}}};
		constexpr std::array<uint32_t, 6> indices{0, 1, 2, 2, 3, 0};
		return FinishMesh(vertices, indices, Aabb{{-x, 0, -z}, {x, 0, z}, false}, output);
	}

	MeshStatus CreateSphereMeshWithTangents(float radius, MeshData& output) {
		if (!std::isfinite(radius) || radius <= 0)
			return {MeshError::InvalidArgument, "Sphere radius must be finite and positive"};
		try {
			constexpr uint32_t rings = 24, sectors = 48;
			struct SphereVertex {
				Vertex base;
				float tangent[4];
			};
			static_assert(sizeof(SphereVertex) == 48 && offsetof(SphereVertex, tangent) == 32);
			std::vector<SphereVertex> vertices;
			std::vector<uint32_t> indices;
			vertices.reserve((rings + 1) * (sectors + 1));
			indices.reserve(6 * sectors * (rings - 1));
			for (uint32_t i = 0; i <= rings; ++i) {
				const float v = float(i) / rings;
				const float h = (i == 0 || i == rings) ? 0.f : std::sin(Math::Pi * v);
				const float y = std::cos(Math::Pi * v);
				for (uint32_t j = 0; j <= sectors; ++j) {
					const float u = float(j) / sectors;
					const float angle = j == sectors ? 0.f : 2.f * Math::Pi * u;
					const float c = std::cos(angle), s = std::sin(angle);
					const float x = h * c, z = h * s;
					/* 接缝处复制 UV；沿经线 +V 与 N×T 一致，极点也保留有限单位切线。 */
					vertices.push_back({{{radius * x, radius * y, radius * z}, {x, y, z}, {u, v}}, {-s, 0, c, 1}});
					if (i == rings || j == sectors)
						continue;
					const uint32_t a = i * (sectors + 1) + j, b = a + sectors + 1;
					if (i > 0)
						indices.insert(indices.end(), {a, a + 1, b});
					if (i + 1 < rings)
						indices.insert(indices.end(), {a + 1, b + 1, b});
				}
			}
			MeshData candidate;
			candidate.vertexCount = uint32_t(vertices.size());
			candidate.vertexStride = sizeof(SphereVertex);
			candidate.attributes = {{0, VertexFormat::Float3, 0, VertexSemantic::Position},
			    {1, VertexFormat::Float3, 12, VertexSemantic::Normal},
			    {2, VertexFormat::Float2, 24, VertexSemantic::TexCoord0},
			    {3, VertexFormat::Float4, 32, VertexSemantic::Tangent}};
			candidate.vertexData.resize(vertices.size() * sizeof(SphereVertex));
			candidate.indexData.resize(indices.size() * sizeof(uint32_t));
			std::memcpy(candidate.vertexData.data(), vertices.data(), candidate.vertexData.size());
			std::memcpy(candidate.indexData.data(), indices.data(), candidate.indexData.size());
			candidate.submeshes.push_back(
			    {0, uint32_t(indices.size()), 0, 0, candidate.vertexCount, 0, MeshTopology::TriangleList});
			candidate.localBounds = {Math::Vec3{-radius}, Math::Vec3{radius}, false};
			if (const auto status = ValidateMesh(candidate); !status.IsSuccess())
				return status;
			output = std::move(candidate);
			return {};
		} catch (const std::bad_alloc&) {
			return {MeshError::AllocationFailed, "Cannot allocate sphere geometry"};
		}
	}

	MeshStatus CreateCubeMeshWithTangents(float size, MeshData& output) {
		MeshData candidate;
		if (const auto status = CreateCubeMesh(size, candidate); !status.IsSuccess())
			return status;
		try {
			constexpr uint32_t stride = 48;
			std::vector<std::byte> vertices(size_t(candidate.vertexCount) * stride);
			for (uint32_t face = 0; face < 6; ++face) {
				Vertex first, next;
				const size_t sourceOffset = size_t(face) * 4 * sizeof(Vertex);
				std::memcpy(&first, candidate.vertexData.data() + sourceOffset, sizeof(first));
				std::memcpy(&next, candidate.vertexData.data() + sourceOffset + sizeof(Vertex), sizeof(next));
				/* 源面独立UV中v0→v1正好为+U；double平方避免极大/极小尺寸的float溢出或下溢。 */
				const glm::dvec3 edge{double(next.position[0]) - first.position[0],
				    double(next.position[1]) - first.position[1], double(next.position[2]) - first.position[2]};
				const double length = glm::length(edge);
				if (!std::isfinite(length) || length <= 0)
					return {MeshError::InvalidArgument, "Cube face cannot define a finite tangent"};
				const std::array<float, 4> tangent{
				    float(edge.x / length), float(edge.y / length), float(edge.z / length), 1};
				for (uint32_t vertex = 0; vertex < 4; ++vertex) {
					const size_t index = size_t(face) * 4 + vertex;
					std::memcpy(vertices.data() + index * stride, candidate.vertexData.data() + index * sizeof(Vertex),
					    sizeof(Vertex));
					std::memcpy(vertices.data() + index * stride + sizeof(Vertex), tangent.data(), sizeof(tangent));
				}
			}
			candidate.vertexData = std::move(vertices);
			candidate.vertexStride = stride;
			candidate.attributes.push_back({3, VertexFormat::Float4, 32, VertexSemantic::Tangent});
			if (const auto status = ValidateMesh(candidate); !status.IsSuccess())
				return status;
			output = std::move(candidate);
			return {};
		} catch (const std::bad_alloc&) {
			return {MeshError::AllocationFailed, "Cannot allocate tangent cube mesh"};
		}
	}
}
