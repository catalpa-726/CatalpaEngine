#pragma once
#include "Animation/AnimationClip.h"
#include "Geometry/MeshAsset.h"

namespace Catalpa {
	/* GPU 可直接消费的 32 字节独立流；顺序是固定 Skeleton 版本的 joints 顺序。 */
	struct SkinInfluence {
		std::array<uint32_t, 4> joints{};
		std::array<float, 4> weights{1, 0, 0, 0};
	};
	static_assert(sizeof(SkinInfluence) == 32);
	struct MeshSkinBinding {
		AssetId skeleton, mesh;
		AssetVersionRef<SkeletonAsset> skeletonVersion;
		AssetVersionRef<MeshAsset> meshVersion;
		Math::Mat4 meshToSkeleton{1};
		/*
		 * 可选绑定变换将网格局部坐标映射到关节绑定姿态下的局部坐标。
		 * 缺省时使用 inverseBind(skeleton) * meshToSkeleton，以兼容共用绑定姿态的旧资产。
		 */
		std::vector<Math::Mat4> perJointInverseBind;
		std::vector<SkinInfluence> influences;
	};
	/**
	 * @brief 校验网格与骨骼的绑定。
	 *
	 * @param[in] binding 待核对网格与骨架版本、顶点影响和绑定矩阵的蒙皮数据。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateMeshSkinBinding(const MeshSkinBinding&, std::string& error);
	/**
	 * @brief 构建蒙皮调色板。
	 *
	 * @param[in] skeleton 定义关节顺序及逆绑定变换的骨架。
	 * @param[in] pose 与绑定骨架对应的当前姿态，其模型空间矩阵按关节索引排列。
	 * @param[in] meshToSkeleton 将网格局部坐标映射到骨架空间的绑定矩阵。
	 * @param[out] out 成功时接收按关节顺序排列的网格局部空间蒙皮矩阵。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BuildSkinningPalette(const SkeletonAsset&, const AnimationPose&, const Math::Mat4& meshToSkeleton,
	    std::vector<Math::Mat4>&, std::string& error);
	/**
	 * @brief 构建蒙皮调色板。
	 *
	 * @param[in] binding 固定网格与骨架版本并描述网格到关节空间变换的蒙皮绑定。
	 * @param[in] pose 与绑定骨架对应的当前姿态，其模型空间矩阵按关节索引排列。
	 * @param[out] output 成功时接收按关节顺序排列的网格局部空间蒙皮矩阵。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BuildSkinningPalette(
	    const MeshSkinBinding&, const AnimationPose&, std::vector<Math::Mat4>&, std::string& error);
	class CATALPA_API ValidatedSkinEvaluator final {
	public:
		/**
		 * @brief 验证并固定供后续求值使用的蒙皮绑定与依赖版本。
		 *
		 * @param[in] binding 带版本的蒙皮绑定。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Bind(AssetVersionRef<MeshSkinBinding>, std::string& error);
		/**
		 * @brief 构建调色板。
		 *
		 * @param[in] pose 与绑定骨架对应的当前姿态，其模型空间矩阵按关节索引排列。
		 * @param[out] output 成功时接收按关节顺序排列的网格局部空间蒙皮矩阵。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool BuildPalette(const AnimationPose&, std::vector<Math::Mat4>&, std::string& error) const;
		const AssetVersionRef<MeshSkinBinding>& Binding() const noexcept { return m_Binding; }

	private:
		AssetVersionRef<MeshSkinBinding> m_Binding;
		Math::Mat4 m_MeshFromSkeleton{1};
		std::vector<Math::Mat4> m_BindFromMesh;
	};
	/**
	 * @brief 在 CPU 上计算蒙皮网格，供核对 GPU 蒙皮结果。
	 * CPU 参考值供 GPU 结果对照；不会代替 SkinningPass，也不修改源网格。
	 *
	 * @param[in] source 待蒙皮的源网格。
	 * @param[in] influences 按源网格顶点排列的关节索引和权重，每个顶点的四个权重之和须在 1 的容差范围内。
	 * @param[in] palette 按关节索引排列的有限仿射蒙皮矩阵，供各顶点混合其关节变换。
	 * @param[out] out 成功时接收按关节权重变形后的网格，含更新的顶点数据和局部包围盒。
	 * @param[out] error 失败时接收具体诊断。
	 *
	 * @return 所有顶点的参考蒙皮结果生成成功时为 true，否则为 false。
	 */
	CATALPA_API bool SkinMeshReference(const MeshData&, std::span<const SkinInfluence>,
	    std::span<const Math::Mat4> palette, MeshData&, std::string& error);
} /* 命名空间 Catalpa */
