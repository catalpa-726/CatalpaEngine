#pragma once
#include "RHI/RHIHandle.h"

#include <vector>

namespace Catalpa {
	class RHIDevice;
	/* 渲染图临时物理缓冲区的唯一所有者。销毁计划时归还所有权，在途命令保留其原生引用。 */
	struct RGTransientStorage final {
		explicit RGTransientStorage(RHIDevice& value) : device(value) {}
		~RGTransientStorage();
		RHIDevice& device;
		std::vector<BufferHandle> buffers;
	};
} /* 命名空间 Catalpa */
