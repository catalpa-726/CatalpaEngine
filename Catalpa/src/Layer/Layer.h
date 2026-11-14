#pragma once

#include <string>

#include "Core/Base.h"
#include "Core/Deltatime.h"
#include "Events/Event.h"

namespace Catalpa {

	class CATALPA_API Layer {
	public:
		Layer(const std::string& name = "Layer");
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnImGuiRender() {}
		virtual void OnUpdate(Deltatime st) {}
		virtual void OnEvent(Event& event) {}

		const std::string& GetName() const { return m_DebugName; }

	protected:
		std::string m_DebugName;
	};

}