#pragma once
#include "catpch.h"
namespace Catalpa {
	/* SHA-256覆盖源内容、完整依赖快照、工具指纹、选项、entry和目标；不使用地址/typeid哈希。 */
	struct ShaderVariantKey {
		std::string sha256;
		/* 编译前源/模块/define/工具/target/规则摘要；最终key另绑定最终字节码。 */
		std::string inputFingerprint;
		bool operator==(const ShaderVariantKey&) const = default;
	};
}
