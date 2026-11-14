#pragma once
#include "catpch.h"
namespace Catalpa {
	struct NetworkMessage {
		std::vector<std::byte> payload;
	};
	struct NetworkLimits {
		size_t maxMessageBytes = 64 * 1024, maxQueuedBytes = 1024 * 1024, maxMessages = 256;
	};
} /* 命名空间 Catalpa */
