#include "catpch.h"
#include "RHIBackendFactory.h"

#include "Log/Log.h"

namespace Catalpa {

	struct RHIBackendFactory::Registry {
		std::unordered_map<GraphicsBackend, CreateFunction> creators;
	};

	RHIBackendFactory::RHIBackendFactory() : m_Registry(std::make_unique<Registry>()) {}

	RHIBackendFactory::~RHIBackendFactory() = default;

	void RHIBackendFactory::Register(GraphicsBackend backend, CreateFunction create) {
		CAT_CORE_ASSERT(create, "Backend creation function is null!");
		m_Registry->creators.insert_or_assign(backend, create);
	}

	std::unique_ptr<RHIDevice> RHIBackendFactory::Create(
	    const RHIBackendDesc& desc, Window& window, RHIStatus& status) const {
		const auto it = m_Registry->creators.find(desc.backend);

		if (it == m_Registry->creators.end()) {
			status = {.code = RHIErrorCode::Unsupported, .operation = "RHIBackendFactory::Create"};
			return nullptr;
		}

		return it->second(window, desc, status);
	}

}