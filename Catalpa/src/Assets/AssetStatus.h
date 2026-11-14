#pragma once
#include "catpch.h"
namespace Catalpa {
	/* CPU 资产的发布状态；Ready 只表示 CPU 数据可用，GPU 驻留和音频设备就绪情况须分别判断。 */
	enum class AssetStatus { Unloaded, WaitingForDependencies, Loading, Ready, Failed, Cancelled };
}
