#pragma once

#include "catpch.h"

#include "Core/Math/CoordinateConvention.h"

namespace Catalpa {

	enum class ProjectionType { Perspective, Orthographic };

	/* 作者镜头采用右手坐标系，朝向负 Z 轴。角度以弧度计，距离以米计，数据独立于窗口和后端对象。 */
	struct CameraLens {
		ProjectionType projection = ProjectionType::Perspective;
		float verticalFovRadians = Math::Pi / 3.0f;
		float nearPlane = 0.1f;
		float farPlane = 1000.0f;
		float orthographicHeight = 10.0f;
		bool infiniteFar = false;
		bool reversedZ = false;
		constexpr bool operator==(const CameraLens&) const = default;
	};

	/*
	 * 像素范围属于各个视图。抖动量以像素为单位，方向取最终 NDC 轴的正向。
	 * Vulkan 视口高度为正时，Y 正方向朝下。只在生成投影时应用一次 flipY。
	 */
	struct CameraViewport {
		uint32_t width = 1;
		uint32_t height = 1;
		float jitterPixelsX = 0.0f;
		float jitterPixelsY = 0.0f;
		bool flipY = true;
		constexpr bool operator==(const CameraViewport&) const = default;
	};

}
