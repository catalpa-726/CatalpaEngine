#include "catpch.h"

#include "Terrain/TerrainAsset.h"

namespace Catalpa {
	namespace {
		struct Vertex {
			float position[3], normal[3], uv[2], tangent[4];
		};
		static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, tangent) == 32);

		bool TileRange(const TerrainHeightData& data, uint32_t x, uint32_t z, uint32_t lod, std::string& error) {
			const auto& s = data.settings;
			if (!ValidateTerrainSettings(s, error))
				return false;
			if (data.heights.size() != size_t(s.width) * s.height || x >= (s.width - 1) / s.tileQuads ||
			    z >= (s.height - 1) / s.tileQuads || lod >= s.lodCount) {
				error = "Terrain tile range or sample count invalid";
				return false;
			}
			return true;
		}

		float Height(const TerrainHeightData& data, uint32_t x, uint32_t z) {
			const auto h = data.heights[size_t(z) * data.settings.width + x];
			if (!std::isfinite(h) || std::abs(h) > 100000)
				throw std::invalid_argument("Tile consumes an invalid height sample");
			return h;
		}

		Math::Vec3 Position(const TerrainHeightData& data, uint32_t x, uint32_t z) {
			return {x * data.settings.spacing, Height(data, x, z), z * data.settings.spacing};
		}

		Vertex Sample(const TerrainHeightData& data, uint32_t x, uint32_t z) {
			const auto& s = data.settings;
			const auto x0 = x ? x - 1 : x, x1 = std::min(x + 1, s.width - 1), z0 = z ? z - 1 : z,
			           z1 = std::min(z + 1, s.height - 1);
			const float dx = (Height(data, x1, z) - Height(data, x0, z)) / ((x1 - x0) * s.spacing),
			            dz = (Height(data, x, z1) - Height(data, x, z0)) / ((z1 - z0) * s.spacing);
			const auto n = glm::normalize(Math::Vec3{-dx, 1, -dz}), t = glm::normalize(Math::Vec3{1, dx, 0}),
			           p = Position(data, x, z);
			return {{p.x, p.y, p.z}, {n.x, n.y, n.z}, {float(x) / (s.width - 1), float(z) / (s.height - 1)},
			    {t.x, t.y, t.z, -1}};
		}

		void Triangles(uint32_t quads, std::vector<uint32_t>& indices) {
			const auto stride = quads + 1;
			for (uint32_t z = 0; z < quads; ++z)
				for (uint32_t x = 0; x < quads; ++x) {
					const auto a = z * stride + x, b = a + 1, c = a + stride, d = c + 1;
					indices.insert(indices.end(), {a, c, b, b, c, d});
				}
		}
	} /* 命名空间 */
	bool BuildTerrainTile(const TerrainHeightData& data, uint32_t tileX, uint32_t tileZ, uint32_t lod,
	    MeshAsset& output, std::string& error) try {
		if (!TileRange(data, tileX, tileZ, lod, error))
			return false;
		const auto& s = data.settings;
		const uint32_t step = uint32_t(1) << lod, n = s.tileQuads / step, stride = n + 1;
		std::vector<Vertex> vertices;
		vertices.reserve(size_t(stride) * stride + size_t(n) * 8);
		std::vector<uint32_t> indices;
		indices.reserve(size_t(n) * n * 6 + size_t(n) * 24);
		/* 全局整数采样：相邻tile共用同一高度、法线和UV，不各自重采样边缘。 */
		for (uint32_t z = 0; z <= n; ++z)
			for (uint32_t x = 0; x <= n; ++x)
				vertices.push_back(Sample(data, tileX * s.tileQuads + x * step, tileZ * s.tileQuads + z * step));
		Triangles(n, indices);
		std::vector<uint32_t> border;
		border.reserve(n * 4);
		for (uint32_t x = 0; x < n; ++x)
			border.push_back(x);
		for (uint32_t z = 0; z < n; ++z)
			border.push_back(z * stride + n);
		for (uint32_t x = n; x > 0; --x)
			border.push_back(n * stride + x);
		for (uint32_t z = n; z > 0; --z)
			border.push_back(z * stride);
		const uint32_t start = uint32_t(vertices.size());
		for (auto i : border) {
			auto vertex = vertices[i];
			vertex.position[1] -= s.skirtDepth;
			vertices.push_back(vertex);
		}
		for (uint32_t i = 0; i < border.size(); ++i) {
			const uint32_t next = (i + 1) % uint32_t(border.size()), a = border[i], b = border[next], c = start + i,
			               d = start + next;
			indices.insert(indices.end(), {a, b, d, a, d, c});
		}
		std::vector<Math::Vec3> points;
		points.reserve(vertices.size());
		for (const auto& v : vertices)
			points.push_back({v.position[0], v.position[1], v.position[2]});
		MeshData mesh;
		mesh.vertexCount = uint32_t(vertices.size());
		mesh.vertexStride = sizeof(Vertex);
		mesh.attributes = {{0, VertexFormat::Float3, 0, VertexSemantic::Position},
		    {1, VertexFormat::Float3, 12, VertexSemantic::Normal},
		    {2, VertexFormat::Float2, 24, VertexSemantic::TexCoord0},
		    {3, VertexFormat::Float4, 32, VertexSemantic::Tangent}};
		mesh.vertexData.resize(vertices.size() * sizeof(Vertex));
		std::memcpy(mesh.vertexData.data(), vertices.data(), mesh.vertexData.size());
		mesh.indexData.resize(indices.size() * 4);
		std::memcpy(mesh.indexData.data(), indices.data(), mesh.indexData.size());
		mesh.submeshes.push_back(
		    {0, uint32_t(indices.size()), 0, 0, uint32_t(vertices.size()), 0, MeshTopology::TriangleList});
		if (!ComputeBounds(points, mesh.localBounds).IsSuccess()) {
			error = "Terrain bounds calculation failed";
			return false;
		}
		MeshAsset value;
		value.materialSlots = {"surface"};
		const auto status = CreateMeshResource(mesh, value.mesh);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		output = std::move(value);
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}

	bool BuildTerrainCollision(const TerrainHeightData& data, uint32_t tileX, uint32_t tileZ,
	    CollisionShapeAsset& output, std::string& error) try {
		if (!TileRange(data, tileX, tileZ, 0, error))
			return false;
		const auto& s = data.settings;
		CollisionShapeAsset value;
		value.kind = CollisionShapeKind::TriangleMesh;
		value.vertices.reserve(size_t(s.tileQuads + 1) * (s.tileQuads + 1));
		value.indices.reserve(size_t(s.tileQuads) * s.tileQuads * 6);
		for (uint32_t z = 0; z <= s.tileQuads; ++z)
			for (uint32_t x = 0; x <= s.tileQuads; ++x)
				value.vertices.push_back(Position(data, tileX * s.tileQuads + x, tileZ * s.tileQuads + z));
		/* 碰撞使用最高分辨率表面，不包含仅用于遮挡LOD裂缝的裙边。 */
		Triangles(s.tileQuads, value.indices);
		if (!ValidateCollisionShape(value, error))
			return false;
		output = std::move(value);
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
} /* 命名空间 Catalpa */
