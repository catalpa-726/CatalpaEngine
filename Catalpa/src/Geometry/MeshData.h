#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "Geometry/Bounds.h"
#include "Geometry/Submesh.h"
#include "Geometry/VertexLayout.h"

namespace Catalpa {

	enum class MeshIndexType { UInt16, UInt32 };

	/**
	 * @brief 返回网格索引字节大小。
	 *
	 * @param[in] type 索引缓冲区的元素类型，用于确定每个索引占用的字节数。
	 *
	 * @return 16 位、32 位索引分别返回 2、4 字节；未知格式返回 0。
	 */
	[[nodiscard]] constexpr uint32_t GetMeshIndexByteSize(MeshIndexType type) noexcept {
		switch (type) {
		case MeshIndexType::UInt16:
			return 2;
		case MeshIndexType::UInt32:
			return 4;
		default:
			return 0;
		}
	}

	/*
	 * 作者或生成器持有的 CPU 几何数据。目前仅支持单个交错浮点顶点流和三角形列表。
	 * 蒙皮权重、LOD 关系与资产身份由独立的领域对象关联，保留原始几何数据。
	 */
	struct MeshData {
		uint32_t vertexCount = 0;
		uint32_t vertexStride = 0;
		/* 顶点布局借用此数组，修改或销毁 MeshData 会使借用失效。 */
		std::vector<VertexAttribute> attributes;
		std::vector<std::byte> vertexData;
		MeshIndexType indexType = MeshIndexType::UInt32;
		std::vector<std::byte> indexData;
		std::vector<Submesh> submeshes;
		Aabb localBounds;

		[[nodiscard]] VertexLayout GetVertexLayout() const noexcept { return {vertexStride, attributes}; }
		[[nodiscard]] size_t GetIndexCount() const noexcept {
			const auto size = GetMeshIndexByteSize(indexType);
			return size != 0 ? indexData.size() / size : 0;
		}
	};

	enum class MeshError {
		None,
		InvalidArgument,
		InvalidLayout,
		InvalidIndex,
		InvalidSubmesh,
		InvalidBounds,
		NonFinite,
		OutOfRange,
		AllocationFailed
	};
	struct MeshStatus {
		MeshError code = MeshError::None;
		const char* message = "";
		[[nodiscard]] bool IsSuccess() const noexcept { return code == MeshError::None; }
	};

	class MeshResource;
	using MeshResourceRef = std::shared_ptr<const MeshResource>;
	/**
	 * @brief 创建网格资源。
	 *
	 * @param[in] source 输入的 MeshData。
	 * @param[out] output 接收 MeshResourceRef 结果。
	 *
	 * @return 网格处理状态；失败时可据此定位数据或布局问题。
	 */
	[[nodiscard]] CATALPA_API MeshStatus CreateMeshResource(const MeshData& source, MeshResourceRef& output);
	/**
	 * @brief 校验网格资源。
	 *
	 * @param[in] resource 包含待检查的顶点布局、顶点与索引数据的网格资源。
	 *
	 * @return 网格处理状态；失败时可据此定位数据或布局问题。
	 */
	[[nodiscard]] CATALPA_API MeshStatus ValidateMeshResource(const MeshResourceRef& resource) noexcept;

	/*
	 * 唯一工厂在完整校验后深复制到真正 const 对象，调用者不能留存可写别名。
	 * 这里只是内置 CPU 几何保活值，不注册资产、创建 GPU 缓冲或持有 Scene。
	 */
	class CATALPA_API MeshResource {
	public:
		[[nodiscard]] const MeshData& GetData() const noexcept { return m_Data; }
		~MeshResource() = default;
		MeshResource(const MeshResource&) = delete;
		MeshResource& operator=(const MeshResource&) = delete;

	private:
		/**
		 * @brief 创建网格资源。
		 * @details 第 1 个参数（const MeshData&）为输入。输入的 MeshData。
		 * @details 第 2 个参数（MeshResourceRef&）为输出。接收 MeshResourceRef 结果。
		 *
		 * @return 网格处理状态；失败时可据此定位数据或布局问题。
		 */
		friend MeshStatus CreateMeshResource(const MeshData&, MeshResourceRef&);
		explicit MeshResource(const MeshData& source) : m_Data(source) {}
		const MeshData m_Data;
	};
}
