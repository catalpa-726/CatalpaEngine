#pragma once
#include "Scripting/ScriptRuntime.h"
#include "ECS/SystemContext.h"

namespace Catalpa {
	/* 调度适配器不拥有 VM，也不创建第二个作业调度器。 */
	struct ScriptSystem {
		static void Update(ScriptRuntime& runtime, AssetManager& assets, SystemContext& context) {
			runtime.Tick(context.scene, assets, context.time.fixedDeltaSeconds);
		}
	};
}
