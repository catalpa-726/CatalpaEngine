#pragma once

#include "Lighting/LightParameters.h"
#include "ECS/World.h"

namespace Catalpa {

	/* 光源朝向取实体局部-Z的世界方向；位置由Transform提供，参数不重复保存变换。 */
	struct LightComponent {
		LightParameters parameters;
		bool enabled = true;
	};
	template <> struct ComponentTraits<LightComponent> {
		[[nodiscard]] static CATALPA_API const ComponentType& Get();
	};

}
