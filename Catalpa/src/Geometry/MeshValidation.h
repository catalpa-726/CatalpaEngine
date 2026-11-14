#pragma once

#include "catpch.h"

#include "Geometry/MeshData.h"

namespace Catalpa {

	/* element 按消息表示 attribute/vertex/submesh 的元素序号，不是字节偏移。 */
	struct MeshValidationIssue {
		MeshError code = MeshError::None;
		const char* message = "";
		size_t element = 0;
	};
	struct MeshValidationReport {
		std::vector<MeshValidationIssue> issues;
		bool truncated = false;
	};

	/**
	 * @brief 验证网格顶点布局、索引、子网格和包围数据，并可收集诊断。
	 * 无需 report 时不分配验证状态；提供 report 时最多记录64条，诊断输出允许在失败时更新。
	 * 始终只读 source；几何数据/资源输出的失败保留规则由各生成器/工厂负责。
	 *
	 * @param[in] source 待校验布局、顶点、索引及子网格范围的网格数据。
	 * @param[out] report 可选输出；非空时收集有数量上限的错误明细和截断标记。
	 *
	 * @return 全部检查通过时返回成功，否则返回首个网格错误。
	 */
	[[nodiscard]] CATALPA_API MeshStatus ValidateMesh(const MeshData& source, MeshValidationReport* report = nullptr);
}
