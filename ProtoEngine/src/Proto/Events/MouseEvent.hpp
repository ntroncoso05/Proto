#pragma once

#include "Event.hpp"
#include "Core/MouseCodes.hpp"

namespace Proto {

	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(const float x, const float y)
			: m_MouseX(x), m_MouseY(y) {}

		float GetX() const { return m_MouseX; }
		float GetY() const { return m_MouseY; }

		DEFINE_EVENT_TYPE_CATEGORY(MouseMovedEvent,
			static_cast<int>(EventCategory::Mouse) |
			static_cast<int>(EventCategory::Input))
	private:
		float m_MouseX, m_MouseY;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(const float xOffset, const float yOffset)
			: m_XOffset(xOffset), m_YOffset(yOffset) {}

		float GetXOffset() const { return m_XOffset; }
		float GetYOffset() const { return m_YOffset; }

		DEFINE_EVENT_TYPE_CATEGORY(MouseScrolledEvent,
			static_cast<int>(EventCategory::Mouse) |
			static_cast<int>(EventCategory::Input))
	private:
		float m_XOffset, m_YOffset;
	};

	class MouseButtonPressedEvent : public Event
	{
	public:
		MouseButtonPressedEvent(const MouseCode button)
			: m_Button(button) {}

		MouseCode GetMouseButton() const { return m_Button; }

		DEFINE_EVENT_TYPE_CATEGORY(MouseButtonPressedEvent,
			static_cast<int>(EventCategory::Mouse) |
			static_cast<int>(EventCategory::Input) |
			static_cast<int>(EventCategory::MouseButton))
	private:
		MouseCode m_Button;
	};

	class MouseButtonReleasedEvent : public Event
	{
	public:
		MouseButtonReleasedEvent(const MouseCode button)
			: m_Button(button) {}

		MouseCode GetMouseButton() const { return m_Button; }

		DEFINE_EVENT_TYPE_CATEGORY(MouseButtonReleasedEvent,
			static_cast<int>(EventCategory::Mouse) |
			static_cast<int>(EventCategory::Input) |
			static_cast<int>(EventCategory::MouseButton))
	private:
		MouseCode m_Button;
	};
}