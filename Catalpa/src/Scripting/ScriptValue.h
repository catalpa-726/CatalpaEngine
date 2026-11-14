#pragma once
#include "Core/Base.h"
#include "Core/Memory/ValueMap.h"
#include "Scene/PersistentEntityId.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {
	/*
	 * 数值/字符串界限与作者归档一致，失败不转换或截断原值。
	 * 所有跨脚本调用的值均独占存储；实体引用只保存稳定身份。
	 */
	using ScriptValue = std::variant<bool, int64_t, double, std::string, Math::Vec3, PersistentEntityId>;
	using ScriptValues = ValueMap<std::string, ScriptValue, std::less<>>;
	/**
	 * @brief 校验脚本值。
	 *
	 * @param[in] values 按名称保存的脚本属性或状态值，供边界和类型校验。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateScriptValues(const ScriptValues&, std::string& error);
}
