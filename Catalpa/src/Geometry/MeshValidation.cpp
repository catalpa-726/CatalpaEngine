#include "catpch.h"

#include "Geometry/MeshValidation.h"

namespace Catalpa {
	namespace {
		/**
		 * @brief 从可能未对齐的网格字节流中读取单精度分量。
		 * memcpy 支持未对齐的 CPU 字节流，不以 reinterpret_cast 假定存储对象或对齐。
		 *
		 * @param[in] bytes 存放交错顶点属性的原始字节缓冲区。
		 * @param[in] offset 待取浮点分量相对缓冲区起点的字节偏移；调用者须先验证可读范围。
		 *
		 * @return 从给定字节处复制出的单精度浮点数。
		 */
		float ReadFloat(const std::vector<std::byte>& bytes, size_t offset) noexcept {
			float value;
			std::memcpy(&value, bytes.data() + offset, sizeof(value));
			return value;
		}

		uint32_t ReadIndex(const MeshData& mesh, size_t index) noexcept {
			if (mesh.indexType == MeshIndexType::UInt16) {
				uint16_t value;
				std::memcpy(&value, mesh.indexData.data() + index * sizeof(value), sizeof(value));
				return value;
			}
			uint32_t value;
			std::memcpy(&value, mesh.indexData.data() + index * sizeof(value), sizeof(value));
			return value;
		}

		/**
		 * @brief 检查网格顶点属性的语义与分量格式是否相容。
		 * 网格属性的语义和着色器输入位置分别描述。RHI 创建资源时还要检查硬件能力及实际着色器 ABI。
		 *
		 * @param[in] attribute 需要核对语义与存储格式是否匹配的顶点属性。
		 *
		 * @return 该语义接受声明的格式时为 true，否则为 false。
		 */
		bool SemanticFormatValid(const VertexAttribute& attribute) noexcept {
			switch (attribute.semantic) {
			case VertexSemantic::Position:
			case VertexSemantic::Normal:
				return attribute.format == VertexFormat::Float3;
			case VertexSemantic::TexCoord0:
				return attribute.format == VertexFormat::Float2;
			case VertexSemantic::Color0:
			case VertexSemantic::Tangent:
				return attribute.format == VertexFormat::Float4;
			default:
				return false;
			}
		}
	}

	MeshStatus ValidateMesh(const MeshData& mesh, MeshValidationReport* report) {
		MeshStatus first;
		if (report) {
			report->issues.clear();
			report->truncated = false;
		}
		try {
			/* 报告有界但验证不因截断提前通过；无 report 时没有验证器容器分配。 */
			const auto issue = [&](MeshError code, const char* message, size_t element = 0) {
				if (first.IsSuccess())
					first = {code, message};
				if (report) {
					if (report->issues.size() < 64)
						report->issues.push_back({code, message, element});
					else
						report->truncated = true;
				}
			};
			const uint32_t indexSize = GetMeshIndexByteSize(mesh.indexType);
			if (indexSize == 0)
				issue(MeshError::InvalidIndex, "Unknown mesh index type");
			if (const auto status = ValidateBounds(mesh.localBounds); !status.IsSuccess())
				issue(MeshError::InvalidBounds, status.message);
			if (mesh.vertexCount == 0) {
				if (mesh.vertexStride != 0 || !mesh.attributes.empty() || !mesh.vertexData.empty() ||
				    !mesh.indexData.empty() || !mesh.submeshes.empty() || !mesh.localBounds.empty)
					issue(
					    MeshError::InvalidArgument, "Empty mesh must have empty streams, layout, submeshes and bounds");
				return first;
			}

			bool layoutValid = true;
			const uint64_t requiredVertexBytes = static_cast<uint64_t>(mesh.vertexCount) * mesh.vertexStride;
			if (mesh.vertexStride == 0 || mesh.vertexStride % sizeof(float) != 0 ||
			    mesh.vertexData.size() != requiredVertexBytes) {
				issue(MeshError::InvalidLayout,
				    "Vertex byte length must equal count times a nonzero float-aligned stride");
				layoutValid = false;
			}
			if (mesh.attributes.empty() || mesh.attributes.size() > 32) {
				issue(MeshError::InvalidLayout, "Mesh needs between one and 32 declared attributes");
				return first;
			}
			size_t positionCount = 0;
			const VertexAttribute* position = nullptr;
			for (size_t i = 0; i < mesh.attributes.size(); ++i) {
				const auto& attribute = mesh.attributes[i];
				const uint32_t bytes = GetVertexFormatByteSize(attribute.format);
				if (attribute.semantic == VertexSemantic::Position) {
					++positionCount;
					position = &attribute;
				}
				if (bytes == 0 || !SemanticFormatValid(attribute) || attribute.offset % sizeof(float) != 0 ||
				    attribute.offset > mesh.vertexStride || bytes > mesh.vertexStride - attribute.offset) {
					issue(MeshError::InvalidLayout, "Attribute semantic, format, offset or stride is invalid", i);
					layoutValid = false;
				}
				for (size_t j = 0; j < i; ++j) {
					const auto& other = mesh.attributes[j];
					const uint64_t end = static_cast<uint64_t>(attribute.offset) + bytes;
					const uint64_t otherEnd =
					    static_cast<uint64_t>(other.offset) + GetVertexFormatByteSize(other.format);
					if (attribute.location == other.location || attribute.semantic == other.semantic ||
					    (attribute.offset < otherEnd && other.offset < end)) {
						issue(MeshError::InvalidLayout, "Mesh attributes duplicate location/semantic or overlap", i);
						layoutValid = false;
					}
				}
			}
			if (positionCount != 1) {
				issue(MeshError::InvalidLayout, "Mesh must declare exactly one Float3 position semantic");
				layoutValid = false;
			}

			const bool boundsValid = ValidateBounds(mesh.localBounds).IsSuccess() && !mesh.localBounds.empty;
			if (mesh.localBounds.empty)
				issue(MeshError::InvalidBounds, "Nonempty mesh needs nonempty local bounds");
			if (layoutValid) {
				for (uint32_t vertex = 0; vertex < mesh.vertexCount; ++vertex) {
					const size_t start = static_cast<size_t>(vertex) * mesh.vertexStride;
					for (const auto& attribute : mesh.attributes) {
						const uint32_t components = GetVertexFormatByteSize(attribute.format) / sizeof(float);
						double squaredLength = 0.0;
						bool finite = true;
						for (uint32_t component = 0; component < components; ++component) {
							const float value =
							    ReadFloat(mesh.vertexData, start + attribute.offset + component * sizeof(float));
							finite = finite && std::isfinite(value);
							if (component < 3)
								squaredLength += static_cast<double>(value) * value;
						}
						if (!finite)
							issue(MeshError::NonFinite, "Vertex attribute contains NaN or infinity", vertex);
						else if (attribute.semantic == VertexSemantic::Normal && squaredLength == 0.0)
							issue(MeshError::InvalidArgument, "Normal semantic cannot contain a zero vector", vertex);
						else if (attribute.semantic == VertexSemantic::Tangent) {
							/* xyz 是切线方向，w 只记录重建副切线时的符号。 */
							const float sign = ReadFloat(mesh.vertexData, start + attribute.offset + 3 * sizeof(float));
							if (squaredLength == 0.0 || std::abs(std::abs(sign) - 1.0f) > 1e-5f)
								issue(MeshError::InvalidArgument,
								    "Tangent requires a nonzero direction and handedness +/-1", vertex);
						}
					}
					if (boundsValid) {
						for (int axis = 0; axis < 3; ++axis) {
							const float value =
							    ReadFloat(mesh.vertexData, start + position->offset + axis * sizeof(float));
							if (std::isfinite(value) &&
							    (value < mesh.localBounds.minimum[axis] || value > mesh.localBounds.maximum[axis]))
								issue(MeshError::InvalidBounds, "Local bounds do not contain every declared position",
								    vertex);
						}
					}
				}
			}

			const bool indexBytesValid =
			    indexSize != 0 && !mesh.indexData.empty() && mesh.indexData.size() % indexSize == 0;
			if (!indexBytesValid)
				issue(MeshError::InvalidIndex, "Nonempty mesh needs a complete UInt16 or UInt32 index stream");
			if (mesh.submeshes.empty())
				issue(MeshError::InvalidSubmesh, "Nonempty mesh needs indexed submeshes");
			const size_t indexCount = mesh.GetIndexCount();
			for (size_t i = 0; i < mesh.submeshes.size(); ++i) {
				const auto& submesh = mesh.submeshes[i];
				const uint64_t indexEnd = static_cast<uint64_t>(submesh.firstIndex) + submesh.indexCount;
				const uint64_t vertexEnd = static_cast<uint64_t>(submesh.firstVertex) + submesh.vertexCount;
				if (submesh.topology != MeshTopology::TriangleList || submesh.indexCount == 0 ||
				    submesh.indexCount % 3 != 0 || indexEnd > indexCount || submesh.vertexCount == 0 ||
				    vertexEnd > mesh.vertexCount) {
					issue(MeshError::InvalidSubmesh, "Submesh topology or index/vertex element range is invalid", i);
					continue;
				}
				if (!indexBytesValid)
					continue;
				for (uint64_t element = submesh.firstIndex; element < indexEnd; ++element) {
					const int64_t vertex =
					    static_cast<int64_t>(ReadIndex(mesh, static_cast<size_t>(element))) + submesh.baseVertex;
					if (vertex < submesh.firstVertex || vertex >= static_cast<int64_t>(vertexEnd))
						issue(MeshError::InvalidIndex,
						    "Index plus signed baseVertex is outside the submesh vertex range", i);
				}
			}
			return first;
		} catch (const std::bad_alloc&) {
			return {MeshError::AllocationFailed, "Cannot allocate mesh validation diagnostics"};
		}
	}

	MeshStatus CreateMeshResource(const MeshData& source, MeshResourceRef& output) {
		if (const auto status = ValidateMesh(source); !status.IsSuccess())
			return status;
		try {
			/* 构造源为深复制；即使 source 本身后来被修改，已发布资源也不受影响。 */
			MeshResourceRef candidate(new const MeshResource(source));
			output = std::move(candidate);
			return {};
		} catch (const std::bad_alloc&) {
			return {MeshError::AllocationFailed, "Cannot copy immutable CPU mesh resource"};
		}
	}

	MeshStatus ValidateMeshResource(const MeshResourceRef& resource) noexcept {
		/* 私有构造和 const 持有保证所有非空资源都已经通过同一个工厂校验。 */
		return resource ? MeshStatus{}
		                : MeshStatus{MeshError::InvalidArgument, "Mesh resource reference cannot be null"};
	}
}
