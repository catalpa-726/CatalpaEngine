#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Core/Math/MathTypes.h"
#include "Scene/EntityReference.h"

namespace Catalpa {
	/* 所有长度使用米。烘焙栅格和运行查询有独立容量，不按输入无限增长。 */
	struct NavigationBuildSettings {
		float cellSize = 0.2f, cellHeight = 0.1f;
		float agentHeight = 1.8f, agentRadius = 0.3f, maxClimb = 0.3f, maxSlopeDegrees = 45;
		uint32_t maxCells = 1000000;
	};
	struct NavigationBuildInput {
		NavigationBuildSettings settings;
		std::string buildProfile;
		std::vector<Math::Vec3> vertices;
		std::vector<uint32_t> indices;
		/* 空数组表示默认区域0；否则每三角形一项，范围0..62。 */
		std::vector<uint8_t> areas;
	};
	struct NavigationPathRequest {
		Math::Vec3 start{}, end{}, nearestExtent{2, 4, 2};
		std::array<float, 64> areaCosts = [] {
			std::array<float, 64> a;
			a.fill(1);
			return a;
		}();
		uint16_t includeFlags = 0xffff, excludeFlags = 0;
		uint32_t maxNodes = 4096, maxPolygons = 512, maxPoints = 256;
	};
	struct NavigationPath {
		uint64_t version = 0;
		std::vector<Math::Vec3> points;
	};
	/**
	 * @brief 校验导航设置。
	 *
	 * @param[in] settings 用于生成导航网格的体素尺寸、代理参数及构建选项。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateNavigationSettings(const NavigationBuildSettings&, std::string& error);
	/**
	 * @brief 校验导航输入。
	 *
	 * @param[in] input 输入的 NavigationBuildInput。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateNavigationInput(const NavigationBuildInput&, std::string& error);
} /* 命名空间 Catalpa */
