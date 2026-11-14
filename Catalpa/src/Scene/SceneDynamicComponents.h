#pragma once
#include "Scene/Scene.h"

namespace Catalpa {
	/**
	 * @brief 将领域组件的修改操作排队至场景结构安全点。
	 * 动态领域统一在 Scene 的结构安全点应用值命令。回调不能捕获 SDK/World 借用指针。
	 *
	 * @param[in,out] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] operation 在结构修改安全点针对场景所属世界执行的操作。
	 *
	 * @return 操作成功入队时返回正常状态，否则返回排队或分配错误。
	 */
	CATALPA_API WorldStatus QueueDynamicComponent(Scene&, EntityId, std::function<WorldStatus(World&)> operation);
} /* 命名空间 Catalpa */
