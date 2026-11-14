#pragma once

#include "Camera/CameraLens.h"
#include "Camera/CameraMatrices.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {

	/**
	 * @brief 校验镜头所选投影分支的视场、裁剪距离和深度设置。
	 * 校验当前镜头分支的有效参数；正交无限远不能形成可逆的线性深度映射。
	 *
	 * @param[in] lens 包含投影模式、裁剪距离和深度方向的镜头参数。
	 *
	 * @return 参数可形成受支持的镜头时返回成功；否则返回具体的数学错误。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus ValidateCameraLens(const CameraLens& lens) noexcept;

	/**
	 * @brief 由镜头和像素视口生成投影矩阵及其解析逆矩阵。
	 * 投影使用 [0,1] 深度范围；两个输出须独立存储，同时通过正逆矩阵校验后才写出。
	 *
	 * @param[in] lens 包含投影模式、裁剪距离和深度方向的镜头参数。
	 * @param[in] viewport 将以像素表示的抖动换算到裁剪空间时使用的视口尺寸。
	 * @param[out] projection 接收镜头投影矩阵。
	 * @param[out] inverse 接收逆投影矩阵，须与投影矩阵分别存储。
	 *
	 * @return 正逆投影均通过检验时返回成功，否则返回镜头、视口或数值精度错误。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus BuildCameraProjection(
	    const CameraLens& lens, const CameraViewport& viewport, Math::Mat4& projection, Math::Mat4& inverse) noexcept;

	/**
	 * @brief 从相机刚体姿态和镜头建立视图、投影及其组合矩阵。
	 * 场景与编辑相机共用刚体姿态和投影检查，通过位置精度及正逆残差校验后才替换快照。
	 *
	 * @param[in] rigidWorld 将相机局部空间映射到世界空间的刚体矩阵。
	 * @param[in] lens 包含投影模式、裁剪距离和深度方向的镜头参数。
	 * @param[in] viewport 提供输出尺寸与纵横比的相机视口。
	 * @param[out] output 成功时写入本次相机矩阵快照。
	 *
	 * @return 位置精度、投影及组合矩阵的逆残差均满足要求时返回成功。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus BuildCameraMatrices(const Math::Mat4& rigidWorld, const CameraLens& lens,
	    const CameraViewport& viewport, CameraMatrices& output) noexcept;

}
