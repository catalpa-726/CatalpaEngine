#include "BehaviorGraph/BehaviorGraphCompiler.h"

namespace Catalpa::Tools {
	bool BehaviorGraphCompiler::Compile(std::string_view source, BehaviorGraphAsset& output, std::string& error) {
		BehaviorGraphAsset candidate;
		if (!ReadBehaviorGraph(source, candidate, error) || !ValidateBehaviorGraph(candidate, error))
			return false;
		output = std::move(candidate);
		return true;
	}
}
