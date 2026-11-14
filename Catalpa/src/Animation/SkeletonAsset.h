#pragma once
#include "Assets/AssetHandle.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {
	inline constexpr size_t MaximumSkeletonJoints = 256;
	struct SkeletonJoint {
		StableId id;
		std::string name;
		int32_t parent = -1;
		Math::TransformTRS bindLocal;
		Math::Mat4 inverseBind{1};
	};
	/* 采用列向量约定 model = parent * local。inverseBind 是骨架绑定姿态下模型空间变换的逆矩阵，与 GPU 原生布局无关。 */
	struct SkeletonAsset {
		std::vector<SkeletonJoint> joints;
	};
	/**
	 * @brief 校验骨骼。
	 *
	 * @param[in] asset 待验证关节标识、父索引和绑定变换的骨架。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateSkeleton(const SkeletonAsset&, std::string& error);
	/**
	 * @brief 构建模型姿态。
	 *
	 * @param[in] skeleton 提供关节顺序与父索引，用于逐级合成模型空间矩阵的骨架。
	 * @param[in] local 与骨架关节一一对应的局部平移、旋转和缩放。
	 * @param[out] output 接收 std::vector<Math::Mat4> 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BuildModelPose(
	    const SkeletonAsset&, std::span<const Math::TransformTRS>, std::vector<Math::Mat4>&, std::string& error);
} /* 命名空间 Catalpa */
