#pragma once
#include "catpch.h"

namespace Catalpa {
	enum class MaterialCullMode { None, Back, Front };
	enum class MaterialBlendMode { Opaque, Alpha, Additive };
	/* 实际pass/pipeline兼容性由Renderer决定；CPU资产只保存受限状态策略。 */
	struct MaterialRenderState {
		MaterialCullMode cull = MaterialCullMode::Back;
		bool depthTest = true, depthWrite = true;
		MaterialBlendMode blend = MaterialBlendMode::Opaque;
		bool operator==(const MaterialRenderState&) const = default;
	};
	enum class MaterialStateOverride : uint32_t { Cull = 1, DepthTest = 2, DepthWrite = 4, Blend = 8 };
	struct MaterialStateOverrides {
		std::optional<MaterialCullMode> cull;
		std::optional<bool> depthTest, depthWrite;
		std::optional<MaterialBlendMode> blend;
	};
}
