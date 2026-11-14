#pragma once
#include "catpch.h"
namespace Catalpa {
	struct RHIMemoryHeapBudget {
		uint64_t heapBytes = 0, budgetBytes = 0, usageBytes = 0, allocationBytes = 0, blockBytes = 0;
		bool deviceLocal = false, driverBudget = false;
	};
} /* 命名空间 Catalpa */
