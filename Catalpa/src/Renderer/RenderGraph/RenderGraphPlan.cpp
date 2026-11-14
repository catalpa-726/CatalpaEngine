#include "catpch.h"

#include "RenderGraphPlan.h"

#include "RenderGraphExecutor.h"

namespace Catalpa {
	RHIStatus RenderGraphPlan::Record(RHICommandList& commands) const {
		return RenderGraphExecutor::Record(*this, commands);
	}
}