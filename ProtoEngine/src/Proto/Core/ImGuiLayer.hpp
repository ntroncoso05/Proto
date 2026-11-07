#pragma once

#include "Layer.hpp"
#include "Events/Event.hpp"

namespace Proto {

	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		virtual ~ImGuiLayer() = default;

		virtual void OnEvent(Event& event) override;

		// --- NEW: Architectural State Lookups ---
		inline bool IsCursorLocked() const { return m_CursorLocked; }

	private:
		bool m_CursorLocked = false;
	};

}