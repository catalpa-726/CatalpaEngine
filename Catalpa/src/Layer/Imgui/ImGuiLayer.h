#pragma once

#include "Layer/Layer.h"
//#include <vulkan/vulkan.h>

#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Events/ApplicationEvent.h"

namespace Catalpa {

	class CATALPA_API ImGuiLayer : public Layer {

	public:
		ImGuiLayer();
		~ImGuiLayer();

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		void OnImGuiRender() override;

		void Begin();
		void End();

		void OnEvent(Event& event);
	private:
		float m_Time = 0.0f;
	};

}